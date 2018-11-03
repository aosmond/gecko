/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ClipManager.h"

#include "DisplayItemClipChain.h"
#include "FrameMetrics.h"
#include "LayersLogging.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDisplayList.h"
#include "nsStyleStructInlines.h"
#include "UnitTransforms.h"

#define CLIP_LOG(...)
//#define CLIP_LOG(...) printf_stderr("CLIP: " __VA_ARGS__)
//#define CLIP_LOG(...) if (XRE_IsContentProcess()) printf_stderr("CLIP: " __VA_ARGS__)

namespace mozilla {
namespace layers {

ClipManager::ClipManager()
  : mManager(nullptr)
  , mBuilder(nullptr)
{
}

inline const ActiveScrolledRoot*
ClipManager::GetItemASR(nsDisplayItem* aItem)
{
  if (aItem->GetType() == DisplayItemType::TYPE_STICKY_POSITION) {
    // For sticky position items, the ASR is computed differently depending
    // on whether the item has a fixed descendant or not. But for WebRender
    // purposes we always want to use the ASR that would have been used if it
    // didn't have fixed descendants, which is stored as the "container ASR" on
    // the sticky item.
    return static_cast<nsDisplayStickyPosition*>(aItem)->GetContainerASR();
  }
  return aItem->GetActiveScrolledRoot();
}

inline int32_t
ClipManager::GetItemAppUnitsPerDevPixel(nsDisplayItem* aItem)
{
  // Zoom display items report their bounds etc using the parent document's
  // APD because zoom items act as a conversion layer between the two different
  // APDs.
  if (aItem->GetType() == DisplayItemType::TYPE_ZOOM) {
    return static_cast<nsDisplayZoom*>(aItem)->GetParentAppUnitsPerDevPixel();
  }
  return aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
}

inline bool
ClipManager::GetItemSeparateClipLeaf(nsDisplayItem* aItem,
                                     const DisplayItemClipChain* aChain,
                                     const ActiveScrolledRoot* aASR)
{
  // In most cases we can combine the leaf of the clip chain with the clip rect
  // of the display item. This reduces the number of clip items, which avoids
  // some overhead further down the pipeline.
  if (aChain && aChain->mASR == aASR &&
      aChain->mClip.GetRoundedRectCount() == 0) {
    if (aItem->GetType() == DisplayItemType::TYPE_TEXT) {
      // Text with shadows interprets the text display item clip rect and
      // clips from the clip chain differently.
      return !aItem->Frame()->StyleText()->HasTextShadow();
    }
    // Container display items are not currently supported because the clip
    // rect of a stacking context is not handled the same as normal display
    // items.
    return aItem->GetChildren() == nullptr;
  }
  return false;
}

inline Maybe<wr::WrClipId>
ClipManager::GetItemClipsScrollLayer(const DisplayItemClipChain* aChain,
                                     const ActiveScrolledRoot* aASR)
{
  if (aChain && aChain->mASR == aASR) {
    // If the clip's ASR is the same as the item's ASR, then we want to use
    // the clip as the "scrollframe" for the item, as WR will do the right thing
    // when building the ClipScrollTree and ensure the item scrolls with the
    // ASR. Note in particular that we don't want to use scroll id of |asr| here
    // because we might have a situation where there is a stacking context
    // between |asr| and |aItem|, and if we used |asr|'s scroll id, then WR
    // would effectively hoist the item out of the stacking context and attach
    // it directly to |asr|. This can produce incorrect results. Using the clip
    // instead of the ASR is strictly better because the clip is usually defined
    // inside the stacking context, and so the item also stays "inside" the
    // stacking context rather than geting hoisted out. Note that there might
    // be cases where the clip is also "outside" the stacking context and in
    // theory that situation might not be handled correctly, but I haven't seen
    // it in practice so far.
    const ClipIdMap& cache = mCacheStack.top();
    auto it = cache.find(aChain);
    MOZ_ASSERT(it != cache.end());
    return Some(it->second);
  }

  if (aChain) {
    // If the clip's ASR is different, then we need to set the scroll id
    // explicitly to match the desired ASR.
    Maybe<wr::WrClipId> scrollId = GetScrollLayer(aASR);
    MOZ_ASSERT(scrollId.isSome());
    return ClipIdAfterOverride(scrollId);
  }

  // If we don't have a clip at all, then we don't want to explicitly push
  // the ASR either, because as with the first clause of this if condition,
  // the item might get hoisted out of a stacking context that was pushed
  // between the |asr| and this |aItem|. Instead we just leave clips.mScrollId
  // empty and things seem to work out.
  // XXX: there might be cases where things don't just "work out", in which
  // case we might need to do something smarter here.
  return Nothing();
}

void
ClipManager::BeginBuild(WebRenderLayerManager* aManager,
                        wr::DisplayListBuilder& aBuilder)
{
  CLIP_LOG("Begin build\n");
  MOZ_ASSERT(!mManager);
  mManager = aManager;
  MOZ_ASSERT(!mBuilder);
  mBuilder = &aBuilder;
  MOZ_ASSERT(mCacheStack.empty());
  mCacheStack.emplace();
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void
ClipManager::EndBuild()
{
  CLIP_LOG("End build\n");
  mBuilder = nullptr;
  mManager = nullptr;
  mCacheStack.pop();
  MOZ_ASSERT(mCacheStack.empty());
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void
ClipManager::BeginList(nsDisplayItem* aItem,
                       const StackingContextHelper& aStackingContext)
{
  bool invalidate = false;
  if (aStackingContext.AffectsClipPositioning()) {
    invalidate = PushOverrideForASR(
        mItemClipStack.empty() ? nullptr : mItemClipStack.top().mASR,
        aStackingContext.ReferenceFrameId());
  }

  // We only need to invalidate if the override affects the current clip and
  // scroll information.
  ItemClips clips(mItemClipStack.empty() ? nullptr : &mItemClipStack.top());
  if (!invalidate) {
    mItemClipStack.push(clips);
    return;
  }

#ifdef DEBUG
  // If we need to invalidate, that means there was already something on the
  // clip stack. It should match the given item since we would have already
  // processed it in BeginItem, except some items like nsDisplayTransform or
  // nsDisplayPerspective, which contain an nsDisplayWrapList, and do not
  // subclass it directly. If the item does not provide an ASR or clip, it just
  // accepted its parent values, so it won't match.
  if (aItem->GetType() != DisplayItemType::TYPE_WRAP_LIST) {
    const DisplayItemClipChain* itemClip = aItem->GetClipChain();
    const ActiveScrolledRoot* itemASR = GetItemASR(aItem);
    if (itemClip || itemASR) {
      MOZ_ASSERT(clips.mASR == itemASR);
      MOZ_ASSERT(clips.mChain == itemClip);
      MOZ_ASSERT(clips.mSeparateLeaf == GetItemSeparateClipLeaf(aItem, itemClip, itemASR));
    }
  }
#endif

  // If the leaf of the clip chain is going to be merged with the display item's
  // clip rect, then we should create a clip chain id from the leaf's parent.
  const DisplayItemClipChain* clip = clips.mChain;
  if (clips.mSeparateLeaf) {
    clip = clip->mParent;
  }

  // Define all the clips in the item's clip chain, and obtain a clip chain id
  // for it. All of the necessary scroll IDs should have been created already.
  int32_t auPerDevPixel = GetItemAppUnitsPerDevPixel(aItem);
  clips.mClipChainId = DefineClipChain(clip, auPerDevPixel, aStackingContext);
  clips.mScrollId = GetItemClipsScrollLayer(clip, clips.mASR);

  // Now that we have the scroll id and a clip id for the item, push it onto
  // the WR stack.
  clips.Apply(mBuilder, auPerDevPixel);
  mItemClipStack.push(clips);
}

void
ClipManager::EndList(const StackingContextHelper& aStackingContext)
{
  MOZ_ASSERT(!mItemClipStack.empty());
  mItemClipStack.top().Unapply(mBuilder);
  mItemClipStack.pop();

  if (aStackingContext.AffectsClipPositioning()) {
    PopOverrideForASR(
        mItemClipStack.empty() ? nullptr : mItemClipStack.top().mASR);
  }
}

bool
ClipManager::PushOverrideForASR(const ActiveScrolledRoot* aASR,
                                const Maybe<wr::WrClipId>& aClipId)
{
  Maybe<wr::WrClipId> scrollId = GetScrollLayer(aASR);
  MOZ_ASSERT(scrollId.isSome());

  CLIP_LOG("Pushing override %zu -> %s\n", scrollId->id,
      aClipId ? Stringify(aClipId->id).c_str() : "(none)");
  auto it = mASROverride.insert({ *scrollId, std::stack<Maybe<wr::WrClipId>>() });
  it.first->second.push(aClipId);

  // Start a new cache
  mCacheStack.emplace();

  // Check if the currently applied clips are affected by the override.
  if (mItemClipStack.empty()) {
    return false;
  }

  const ItemClips& top = mItemClipStack.top();
  if (top.mScrollId == scrollId) {
    CLIP_LOG("applied clip scroll ID overridden\n");
    return true;
  }

  for (const DisplayItemClipChain* chain = top.mChain; chain; chain = chain->mParent) {
    FrameMetrics::ViewID viewId = chain->mASR
        ? chain->mASR->GetViewId()
        : ScrollableLayerGuid::NULL_SCROLL_ID;
    Maybe<wr::WrClipId> chainScrollId =
      mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
    if (chainScrollId == scrollId) {
      CLIP_LOG("applied clip chain %p scroll ID overridden\n", chain);
      return true;
    }
  }

  return false;
}

void
ClipManager::PopOverrideForASR(const ActiveScrolledRoot* aASR)
{
  MOZ_ASSERT(!mCacheStack.empty());
  mCacheStack.pop();

  Maybe<wr::WrClipId> scrollId = GetScrollLayer(aASR);
  MOZ_ASSERT(scrollId.isSome());

  auto it = mASROverride.find(*scrollId);
  MOZ_ASSERT(it != mASROverride.end());
  MOZ_ASSERT(!(it->second.empty()));
  CLIP_LOG("Popping override %zu -> %s\n", scrollId->id,
      it->second.top() ? Stringify(it->second.top()->id).c_str() : "(none)");
  it->second.pop();
  if (it->second.empty()) {
    mASROverride.erase(it);
  }
}

Maybe<wr::WrClipId>
ClipManager::ClipIdAfterOverride(const Maybe<wr::WrClipId>& aClipId)
{
  if (!aClipId) {
    return Nothing();
  }
  auto it = mASROverride.find(*aClipId);
  if (it == mASROverride.end()) {
    return aClipId;
  }
  MOZ_ASSERT(!it->second.empty());
  CLIP_LOG("Overriding %zu with %s\n", aClipId->id,
      it->second.top() ? Stringify(it->second.top()->id).c_str() : "(none)");
  return it->second.top();
}

void
ClipManager::BeginItem(nsDisplayItem* aItem,
                       const StackingContextHelper& aStackingContext)
{
  CLIP_LOG("processing item %p (%s)\n",
      aItem, DisplayItemTypeName(aItem->GetType()));

  const DisplayItemClipChain* clip = aItem->GetClipChain();
  const ActiveScrolledRoot* asr = GetItemASR(aItem);
  bool separateLeaf = GetItemSeparateClipLeaf(aItem, clip, asr);

  ItemClips clips(asr, clip, separateLeaf);
  MOZ_ASSERT(!mItemClipStack.empty());
  if (clips.HasSameInputs(mItemClipStack.top())) {
    // Early-exit because if the clips are the same as aItem's previous sibling,
    // then we don't need to do do the work of popping the old stuff and then
    // pushing it right back on for the new item. Note that if aItem doesn't
    // have a previous sibling, that means BeginList would have been called
    // just before this, which will have pushed a ItemClips(nullptr, nullptr)
    // onto mItemClipStack, so the HasSameInputs check should return false.
    CLIP_LOG("early-exit for %p\n", aItem);
    return;
  }

  // Pop aItem's previous sibling's stuff from mBuilder in preparation for
  // pushing aItem's stuff.
  mItemClipStack.top().Unapply(mBuilder);
  mItemClipStack.pop();

  // If the leaf of the clip chain is going to be merged with the display item's
  // clip rect, then we should create a clip chain id from the leaf's parent.
  if (separateLeaf) {
    clip = clip->mParent;
  }

  // There are two ASR chains here that we need to be fully defined. One is the
  // ASR chain pointed to by |asr|. The other is the
  // ASR chain pointed to by clip->mASR. We pick the leafmost
  // of these two chains because that one will include the other. Calling
  // DefineScrollLayers with this leafmost ASR will recursively define all the
  // ASRs that we care about for this item, but will not actually push
  // anything onto the WR stack.
  const ActiveScrolledRoot* leafmostASR = asr;
  if (clip) {
    leafmostASR = ActiveScrolledRoot::PickDescendant(leafmostASR, clip->mASR);
  }
  Maybe<wr::WrClipId> leafmostId = DefineScrollLayers(leafmostASR, aItem, aStackingContext);

  // Define all the clips in the item's clip chain, and obtain a clip chain id
  // for it.
  int32_t auPerDevPixel = GetItemAppUnitsPerDevPixel(aItem);
  clips.mClipChainId = DefineClipChain(clip, auPerDevPixel, aStackingContext);
  clips.mScrollId = GetItemClipsScrollLayer(clip, asr);

  // Now that we have the scroll id and a clip id for the item, push it onto
  // the WR stack.
  clips.Apply(mBuilder, auPerDevPixel);
  mItemClipStack.push(clips);

  CLIP_LOG("done setup for %p\n", aItem);
}

Maybe<wr::WrClipId>
ClipManager::GetScrollLayer(const ActiveScrolledRoot* aASR)
{
  for (const ActiveScrolledRoot* asr = aASR; asr; asr = asr->mParent) {
    Maybe<wr::WrClipId> scrollId =
      mBuilder->GetScrollIdForDefinedScrollLayer(asr->GetViewId());
    if (scrollId) {
      return scrollId;
    }

    // If this ASR doesn't have a scroll ID, then we should check its ancestor.
    // There may not be one defined because the ASR may not be scrollable or we
    // failed to get the scroll metadata.
  }

  Maybe<wr::WrClipId> scrollId =
    mBuilder->GetScrollIdForDefinedScrollLayer(ScrollableLayerGuid::NULL_SCROLL_ID);
  MOZ_ASSERT(scrollId.isSome());
  return scrollId;
}

Maybe<wr::WrClipId>
ClipManager::DefineScrollLayers(const ActiveScrolledRoot* aASR,
                                nsDisplayItem* aItem,
                                const StackingContextHelper& aSc)
{
  if (!aASR) {
    // Recursion base case
    return Nothing();
  }
  ScrollableLayerGuid::ViewID viewId = aASR->GetViewId();
  Maybe<wr::WrClipId> scrollId = mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
  if (scrollId) {
    // If we've already defined this scroll layer before, we can early-exit
    return scrollId;
  }
  // Recurse to define the ancestors
  Maybe<wr::WrClipId> ancestorScrollId = DefineScrollLayers(aASR->mParent, aItem, aSc);

  Maybe<ScrollMetadata> metadata = aASR->mScrollableFrame->ComputeScrollMetadata(
      mManager, aItem->ReferenceFrame(), Nothing(), nullptr);
  if (!metadata) {
    MOZ_ASSERT_UNREACHABLE("Expected scroll metadata to be available!");
    return ancestorScrollId;
  }

  FrameMetrics& metrics = metadata->GetMetrics();
  if (!metrics.IsScrollable()) {
    // This item is a scrolling no-op, skip over it in the ASR chain.
    return ancestorScrollId;
  }

  LayoutDeviceRect contentRect =
      metrics.GetExpandedScrollableRect() * metrics.GetDevPixelsPerCSSPixel();
  LayoutDeviceRect clipBounds =
      LayoutDeviceRect::FromUnknownRect(metrics.GetCompositionBounds().ToUnknownRect());
  // The content rect that we hand to PushScrollLayer should be relative to
  // the same origin as the clipBounds that we hand to PushScrollLayer - that
  // is, both of them should be relative to the stacking context `aSc`.
  // However, when we get the scrollable rect from the FrameMetrics, the origin
  // has nothing to do with the position of the frame but instead represents
  // the minimum allowed scroll offset of the scrollable content. While APZ
  // uses this to clamp the scroll position, we don't need to send this to
  // WebRender at all. Instead, we take the position from the composition
  // bounds.
  contentRect.MoveTo(clipBounds.TopLeft());

  Maybe<wr::WrClipId> parent = ClipIdAfterOverride(ancestorScrollId);
  scrollId = Some(mBuilder->DefineScrollLayer(viewId, parent,
      wr::ToRoundedLayoutRect(contentRect),
      wr::ToRoundedLayoutRect(clipBounds)));

  return scrollId;
}

Maybe<wr::WrClipChainId>
ClipManager::DefineClipChain(const DisplayItemClipChain* aChain,
                             int32_t aAppUnitsPerDevPixel,
                             const StackingContextHelper& aSc)
{
  nsTArray<wr::WrClipId> clipIds;
  // Iterate through the clips in the current item's clip chain, define them
  // in WR, and put their IDs into |clipIds|.
  for (const DisplayItemClipChain* chain = aChain; chain; chain = chain->mParent) {
    ClipIdMap& cache = mCacheStack.top();
    auto it = cache.find(chain);
    if (it != cache.end()) {
      // Found it in the currently-active cache, so just use the id we have for
      // it.
      CLIP_LOG("cache[%p] => %zu\n", chain, it->second.id);
      clipIds.AppendElement(it->second);
      continue;
    }
    if (!chain->mClip.HasClip()) {
      // This item in the chain is a no-op, skip over it
      continue;
    }

    LayoutDeviceRect clip = LayoutDeviceRect::FromAppUnits(
        chain->mClip.GetClipRect(), aAppUnitsPerDevPixel);
    nsTArray<wr::ComplexClipRegion> wrRoundedRects;
    chain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, aSc, wrRoundedRects);

    Maybe<wr::WrClipId> scrollId = GetScrollLayer(chain->mASR);
    // Before calling DefineClipChain we defined the ASRs by calling
    // DefineScrollLayers, so we must have a scrollId here.
    MOZ_ASSERT(scrollId.isSome());

    // Define the clip
    Maybe<wr::WrClipId> parent = ClipIdAfterOverride(scrollId);
    wr::WrClipId clipId = mBuilder->DefineClip(
        parent,
        wr::ToRoundedLayoutRect(clip), &wrRoundedRects);
    clipIds.AppendElement(clipId);
    cache[chain] = clipId;
    CLIP_LOG("cache[%p] <= %zu\n", chain, clipId.id);
  }

  // Now find the parent display item's clipchain id
  Maybe<wr::WrClipChainId> parentChainId;
  if (!mItemClipStack.empty()) {
    parentChainId = mItemClipStack.top().mClipChainId;
  }

  // And define the current display item's clipchain using the clips and the
  // parent. If the current item has no clips of its own, just use the parent
  // item's clipchain.
  Maybe<wr::WrClipChainId> chainId;
  if (clipIds.Length() > 0) {
    chainId = Some(mBuilder->DefineClipChain(parentChainId, clipIds));
  } else {
    chainId = parentChainId;
  }
  return chainId;
}

ClipManager::~ClipManager()
{
  MOZ_ASSERT(!mBuilder);
  MOZ_ASSERT(mCacheStack.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

ClipManager::ItemClips::ItemClips(const ActiveScrolledRoot* aASR,
                                  const DisplayItemClipChain* aChain,
                                  bool aSeparateLeaf,
                                  bool aForList /* = false */)
  : mASR(aASR)
  , mChain(aChain)
  , mSeparateLeaf(aSeparateLeaf)
  , mApplied(false)
  , mForList(aForList)
{
}

ClipManager::ItemClips::ItemClips(const ItemClips* aTop)
  : mASR(aTop ? aTop->mASR : nullptr)
  , mChain(aTop ? aTop->mChain : nullptr)
  , mSeparateLeaf(aTop ? aTop->mSeparateLeaf : false)
  , mApplied(false)
  , mForList(true)
{
  if (aTop) {
    CopyOutputsFrom(*aTop);
  }
}

void
ClipManager::ItemClips::Apply(wr::DisplayListBuilder* aBuilder,
                              int32_t aAppUnitsPerDevPixel)
{
  MOZ_ASSERT(!mApplied);
  mApplied = true;

  Maybe<wr::LayoutRect> clipLeaf;
  if (mSeparateLeaf) {
    MOZ_ASSERT(mChain);
    clipLeaf.emplace(wr::ToRoundedLayoutRect(LayoutDeviceRect::FromAppUnits(
      mChain->mClip.GetClipRect(), aAppUnitsPerDevPixel)));
  }

  aBuilder->PushClipAndScrollInfo(mScrollId.ptrOr(nullptr),
                                  mClipChainId.ptrOr(nullptr),
                                  clipLeaf);
}

void
ClipManager::ItemClips::Unapply(wr::DisplayListBuilder* aBuilder)
{
  if (mApplied) {
    mApplied = false;
    aBuilder->PopClipAndScrollInfo(mScrollId.ptrOr(nullptr));
  }
}

bool
ClipManager::ItemClips::HasSameInputs(const ItemClips& aOther)
{
  if (aOther.mForList) {
    return mASR == nullptr && mChain == nullptr;
  }

  return mASR == aOther.mASR &&
         mChain == aOther.mChain &&
         mSeparateLeaf == aOther.mSeparateLeaf;
}

void
ClipManager::ItemClips::CopyOutputsFrom(const ItemClips& aOther)
{
  mScrollId = aOther.mScrollId;
  mClipChainId = aOther.mClipChainId;
  mSeparateLeaf = aOther.mSeparateLeaf;
}

} // namespace layers
} // namespace mozilla
