/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MediaSourceHandle_h_
#define mozilla_dom_MediaSourceHandle_h_

#include "mozilla/dom/MediaSourceBinding.h"

namespace mozilla::dom {
class MediaSource;
class MediaSourceHandleImpl;

struct MediaSourceHandleCloneData final {
  explicit MediaSourceHandleCloneData(MediaSourceHandleImpl* aImpl);
  ~MediaSourceHandleCloneData();

  RefPtr<MediaSourceHandleImpl> mImpl;
};

class MediaSourceHandle final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(MediaSourceHandle)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(MediaSourceHandle)

  static already_AddRefed<MediaSourceHandle> Create(MediaSource* aMediaSource);

  static already_AddRefed<MediaSourceHandle> Create(
      nsIGlobalObject* aGlobal, MediaSourceHandleCloneData* aCloneData);

  nsIGlobalObject* GetParentObject() const { return mParent; }

  MediaSourceHandleImpl* GetImpl() const { return mImpl; }

  UniquePtr<MediaSourceHandleCloneData> ToCloneData() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  explicit MediaSourceHandle(nsIGlobalObject* aParent,
                             RefPtr<MediaSourceHandleImpl>&& aImpl);
  ~MediaSourceHandle();

  nsCOMPtr<nsIGlobalObject> mParent;
  RefPtr<MediaSourceHandleImpl> mImpl;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_MediaSourceHandle_h_ */
