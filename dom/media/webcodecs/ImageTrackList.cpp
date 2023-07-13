/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrackList.h"

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

}  // namespace mozilla::dom
