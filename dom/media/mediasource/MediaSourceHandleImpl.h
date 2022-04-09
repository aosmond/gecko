/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MediaSourceHandleImpl_h_
#define mozilla_dom_MediaSourceHandleImpl_h_

#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "nsISupportsImpl.h"

namespace mozilla::dom {
class MediaSource;
class ThreadSafeWorkerRef;

class MediaSourceHandleImpl final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaSourceHandleImpl)

  static already_AddRefed<MediaSourceHandleImpl> Create(
      MediaSource* aMediaSource);
  void Destroy();

 private:
  explicit MediaSourceHandleImpl(MediaSource* aMediaSource);
  ~MediaSourceHandleImpl();

  Mutex mMutex;
  RefPtr<ThreadSafeWorkerRef> mWorkerRef GUARDED_BY(mMutex);
  RefPtr<MediaSource> mMediaSource GUARDED_BY(mMutex);
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_MediaSourceHandleImpl_h_ */
