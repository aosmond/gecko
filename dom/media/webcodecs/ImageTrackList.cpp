/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ImageTrack.h"
#include "mozilla/image/ImageUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageTrackList, mParent, mDecoder,
                                      mReadyPromise, mTracks)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageTrackList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageTrackList)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageTrackList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageTrackList::ImageTrackList(nsIGlobalObject* aParent, ImageDecoder* aDecoder)
    : mParent(aParent), mDecoder(aDecoder) {}

ImageTrackList::~ImageTrackList() = default;

JSObject* ImageTrackList::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageTrackList_Binding::Wrap(aCx, this, aGivenProto);
}

void ImageTrackList::Initialize(ErrorResult& aRv) {
  mReadyPromise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }
}

void ImageTrackList::Destroy() {
  if (!mIsReady && mReadyPromise && mReadyPromise->PromiseObj()) {
    mReadyPromise->MaybeRejectWithAbortError("ImageTrackList destroyed");
    mIsReady = true;
  }

  for (auto& track : mTracks) {
    track->Destroy();
  }
  mTracks.Clear();

  mDecoder = nullptr;
  mSelectedIndex = -1;
}

void ImageTrackList::MaybeResolveReady() {
  if (mIsReady) {
    return;
  }

  ImageTrack* track = GetSelectedTrack();
  if (!track || (track->FrameCount() == 0 && !track->FrameCountComplete())) {
    return;
  }

  mReadyPromise->MaybeResolveWithUndefined();
  mIsReady = true;
}

void ImageTrackList::MaybeRejectReady(const nsACString& aReason) {
  if (mIsReady) {
    return;
  }
  mReadyPromise->MaybeRejectWithInvalidStateError(aReason);
  mIsReady = true;
}

void ImageTrackList::OnMetadataSuccess(
    const image::DecodeMetadataResult& aMetadata) {
  MOZ_ASSERT(aMetadata.mTrackCount == 1);

  auto track = MakeRefPtr<ImageTrack>(
      this, /* aIndex */ 0, /* aSelected */ true, aMetadata.mAnimated,
      /* aFrameCount */ 0, aMetadata.mRepetitions);
  mTracks.AppendElement(std::move(track));
  mSelectedIndex = 0;
}

void ImageTrackList::OnMetadataFailed(nsresult aErr) {
  MaybeRejectReady("Metadata decoding failed"_ns);
}

void ImageTrackList::OnFrameCountSuccess(
    const image::DecodeFrameCountResult& aResult) {
  MOZ_ASSERT(aResult.mTrack >= 0);
  if (mTracks.Length() <= static_cast<uint32_t>(aResult.mTrack)) {
    return;
  }

  mTracks[aResult.mTrack]->OnFrameCountSuccess(aResult);
  MaybeResolveReady();
}

void ImageTrackList::SetSelectedIndex(int32_t aIndex, bool aSelected) {
  MOZ_ASSERT(aIndex >= 0);
  MOZ_ASSERT(uint32_t(aIndex) < mTracks.Length());

  // 10.7.2. Attributes - selected, of type boolean

  // 1. If [[ImageDecoder]]'s [[closed]] slot is true, abort these steps.
  if (!mDecoder) {
    return;
  }

  // 2. Let newValue be the given value.
  // 3. If newValue equals [[selected]], abort these steps.
  // 4. Assign newValue to [[selected]].
  // 5. Let parentTrackList be [[ImageTrackList]]
  // 6. Let oldSelectedIndex be the value of parentTrackList [[selected index]].
  // 7. If oldSelectedIndex is not -1:
  // 7.1. Let oldSelectedTrack be the ImageTrack in parentTrackList
  //      [[track list]] at the position of oldSelectedIndex.
  // 7.2. Assign false to oldSelectedTrack [[selected]]
  // 8. If newValue is true, let selectedIndex be the index of this ImageTrack
  //    within parentTrackList's [[track list]]. Otherwise, let selectedIndex be
  //    -1.
  // 9. Assign selectedIndex to parentTrackList [[selected index]].
  if (aSelected) {
    if (mSelectedIndex == -1) {
      MOZ_ASSERT(!mTracks[aIndex]->Selected());
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
    } else if (mSelectedIndex != aIndex) {
      MOZ_ASSERT(mTracks[mSelectedIndex]->Selected());
      MOZ_ASSERT(!mTracks[aIndex]->Selected());
      mTracks[mSelectedIndex]->ClearSelected();
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
    } else {
      MOZ_ASSERT(mTracks[mSelectedIndex]->Selected());
      return;
    }
  } else if (mSelectedIndex == aIndex) {
    mTracks[mSelectedIndex]->ClearSelected();
    mSelectedIndex = -1;
  } else {
    MOZ_ASSERT(!mTracks[aIndex]->Selected());
    return;
  }

  // 10. Run the Reset ImageDecoder algorithm on [[ImageDecoder]].
  mDecoder->Reset();

  // 11. Queue a control message to [[ImageDecoder]]'s control message queue to
  //     update the internal selected track index with selectedIndex.
  mDecoder->QueueSelectTrackMessage(mSelectedIndex);

  // 12. Process the control message queue belonging to [[ImageDecoder]].
  mDecoder->ProcessControlMessageQueue();
}

}  // namespace mozilla::dom
