/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ImageTrack.h"
#include "mozilla/image/ImageUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageTrackList, mParent, mReadyPromise,
                                      mTracks)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageTrackList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageTrackList)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageTrackList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageTrackList::ImageTrackList(nsIGlobalObject* aParent) : mParent(aParent) {}

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

void ImageTrackList::OnMetadataSuccess(
    const image::DecodeMetadataResult& aMetadata) {
  auto track = MakeRefPtr<ImageTrack>(
      this, /* aIndex */ 0, /* aSelected */ true, aMetadata.mAnimated,
      aMetadata.mRepetitions, aMetadata.mFrames);
  mTracks.AppendElement(std::move(track));
  mSelectedIndex = 0;
  mIsReady = true;
  mReadyPromise->MaybeResolveWithUndefined();
}

void ImageTrackList::OnMetadataFailed(nsresult aErr) {
  mIsReady = true;
  mReadyPromise->MaybeRejectWithInvalidStateError("Metadata decoding failed");
}

void ImageTrackList::SetSelectedIndex(int32_t aIndex, bool aSelected) {
  MOZ_ASSERT(aIndex >= 0);
  MOZ_ASSERT(uint32_t(aIndex) < mTracks.Length());

  if (aSelected) {
    if (mSelectedIndex == -1) {
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
    } else if (mSelectedIndex != aIndex) {
      mTracks[mSelectedIndex]->ClearSelected();
      mTracks[aIndex]->MarkSelected();
      mSelectedIndex = aIndex;
    }
  } else if (mSelectedIndex == aIndex) {
    mTracks[mSelectedIndex]->ClearSelected();
    mSelectedIndex = -1;
  }
}

}  // namespace mozilla::dom
