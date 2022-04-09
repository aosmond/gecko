/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaSourceHandle.h"
#include "MediaSourceHandleImpl.h"

namespace mozilla::dom {

MediaSourceHandleCloneData::MediaSourceHandleCloneData(
    MediaSourceHandleImpl* aImpl)
    : mImpl(aImpl) {}

MediaSourceHandleCloneData::~MediaSourceHandleCloneData() = default;

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(MediaSourceHandle, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(MediaSourceHandle, Release)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaSourceHandle, mParent)

/* static */ already_AddRefed<MediaSourceHandle> MediaSourceHandle::Create(
    MediaSource* aMediaSource) {
  RefPtr<MediaSourceHandleImpl> impl =
      MediaSourceHandleImpl::Create(aMediaSource);
  if (!impl) {
    return nullptr;
  }

  RefPtr<MediaSourceHandle> self =
      new MediaSourceHandle(aMediaSource->GetParentObject(), std::move(impl));
  return self.forget();
}

/* static */ already_AddRefed<MediaSourceHandle> MediaSourceHandle::Create(
    nsIGlobalObject* aGlobal, MediaSourceHandleCloneData* aCloneData) {
  RefPtr<MediaSourceHandle> self =
      new MediaSourceHandle(aGlobal, std::move(aCloneData->mImpl));
  return self.forget();
}

MediaSourceHandle::MediaSourceHandle(nsIGlobalObject* aParent,
                                     RefPtr<MediaSourceHandleImpl>&& aImpl)
    : mParent(aParent), mImpl(std::move(aImpl)) {}

MediaSourceHandle::~MediaSourceHandle() = default;

JSObject* MediaSourceHandle::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return MediaSourceHandle_Binding::Wrap(aCx, this, aGivenProto);
}

UniquePtr<MediaSourceHandleCloneData> MediaSourceHandle::ToCloneData() const {
  UniquePtr<MediaSourceHandleCloneData> clone(
      new MediaSourceHandleCloneData(mImpl));
  return clone;
}

}  // namespace mozilla::dom
