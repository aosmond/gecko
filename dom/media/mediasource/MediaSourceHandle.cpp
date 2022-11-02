/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaSourceHandle.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaSourceHandle)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaSourceHandle, mParent)

NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaSourceHandle)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaSourceHandle)

MediaSourceHandle::MediaSourceHandle(nsIGlobalObject* aParent)
    : mParent(aParent) {}

MediaSourceHandle::~MediaSourceHandle() = default;

JSObject* MediaSourceHandle::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return MediaSourceHandle_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
