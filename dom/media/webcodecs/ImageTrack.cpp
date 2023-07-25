/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrack.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageTrack, mParent, mTrackList)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageTrack)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageTrack)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageTrack)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageTrack::ImageTrack(ImageTrackList* aTrackList, int32_t aIndex,
                       bool aSelected, bool aAnimated, uint32_t aFrameCount,
                       float aRepetitionCount)
    : mParent(aTrackList->GetParentObject()),
      mTrackList(aTrackList),
      mIndex(aIndex),
      mRepetitionCount(aRepetitionCount),
      mFrameCount(aFrameCount),
      mAnimated(aAnimated),
      mSelected(aSelected) {}

ImageTrack::~ImageTrack() = default;

JSObject* ImageTrack::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageTrack_Binding::Wrap(aCx, this, aGivenProto);
}

void ImageTrack::SetSelected(bool aSelected) {
  if (mSelected == aSelected) {
    return;
  }

  if (mTrackList) {
    mTrackList->SetSelectedIndex(mIndex, aSelected);
  }
}

}  // namespace mozilla::dom
