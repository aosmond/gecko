/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ImageTrack.h"
#include "mozilla/image/ImageUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ImageTrackList)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageTrackList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageTrackList)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageTrackList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ImageTrackList)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDecoder)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReadyPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTracks)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ImageTrackList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDecoder)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReadyPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTracks)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

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
  if (mReadyPromise &&
      mReadyPromise->State() != Promise::PromiseState::Pending) {
    mReadyPromise->MaybeRejectWithAbortError("ImageDecoder destroyed");
  }

  for (auto& track : mTracks) {
    track->Destroy();
  }
  mTracks.Clear();

  mDecoder = nullptr;
  mSelectedIndex = -1;
}

void ImageTrackList::MaybeResolveReady() {
  ImageTrack* track = GetSelectedTrack();
  if (!track || (track->FrameCount() == 0 && !track->FrameCountComplete())) {
    return;
  }
  if (mReadyPromise->State() != Promise::PromiseState::Pending) {
    mReadyPromise = Promise::CreateInfallible(mParent);
  }
  mReadyPromise->MaybeResolveWithUndefined();
  mIsReady = true;
}

void ImageTrackList::MaybeRejectReady(const nsACString& aReason) {
  if (mReadyPromise->State() != Promise::PromiseState::Pending) {
    mReadyPromise = Promise::CreateInfallible(mParent);
  }
  mReadyPromise->MaybeRejectWithInvalidStateError(aReason);
  mIsReady = true;
}

void ImageTrackList::OnMetadataSuccess(
    const image::DecodeMetadataResult& aMetadata) {
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
  if (mTracks.IsEmpty()) {
    return;
  }

  mTracks.LastElement()->OnFrameCountSuccess(aResult);

  if (!mIsReady) {
    MaybeResolveReady();
  }
}

void ImageTrackList::SetSelectedIndex(int32_t aIndex, bool aSelected) {
  MOZ_ASSERT(aIndex >= 0);
  MOZ_ASSERT(uint32_t(aIndex) < mTracks.Length());

  // 10.7.2.1. If [[ImageDecoder]]'s [[closed]] slot is true, abort these steps.
  if (!mDecoder) {
    return;
  }

  // 10.7.2.2. and 10.7.2.3. handled in ImageTrack::SetSelected.

  // 10.7.2.4. Assign newValue to [[selected]].
  // 10.7.2.5. Let parentTrackList be [[ImageTrackList]]
  // 10.7.2.6. Let oldSelectedIndex be the value of parentTrackList [[selected
  // index]]. 10.7.2.7. If oldSelectedIndex is not -1: 10.7.2.7.1. Let
  // oldSelectedTrack be the ImageTrack in parentTrackList [[track list]] at the
  // position of oldSelectedIndex. 10.7.2.7.2. Assign false to oldSelectedTrack
  // [[selected]] 10.7.2.8. If newValue is true, let selectedIndex be the index
  // of this ImageTrack within parentTrackList’s [[track list]]. Otherwise, let
  // selectedIndex be -1. 10.7.2.9. Assign selectedIndex to parentTrackList
  // [[selected index]].
  if (aSelected) {
    if (mSelectedIndex == -1) {
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
      MaybeResolveReady();
    } else if (mSelectedIndex != aIndex) {
      mTracks[mSelectedIndex]->ClearSelected();
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
      MaybeResolveReady();
    }
  } else if (mSelectedIndex == aIndex) {
    mTracks[mSelectedIndex]->ClearSelected();
    mSelectedIndex = -1;
  }

  // 10.7.2.10. Run the Reset ImageDecoder algorithm on [[ImageDecoder]].
  mDecoder->Reset();

  // FIXME
  // 10.7.2.11. Queue a control message to [[ImageDecoder]]'s control message
  // queue to update the internal selected track index with selectedIndex.
}

}  // namespace mozilla::dom
