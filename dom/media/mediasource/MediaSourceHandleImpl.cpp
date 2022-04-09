/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaSourceHandleImpl.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"

namespace mozilla::dom {

/* static */ already_AddRefed<MediaSourceHandleImpl>
MediaSourceHandleImpl::Create(MediaSource* aMediaSource) {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  if (!workerPrivate) {
    MOZ_ASSERT_UNREACHABLE(
        "Can only create MediaSourceHandleImpl on worker thread!");
    return nullptr;
  }

  RefPtr<MediaSourceHandleImpl> self = new MediaSourceHandleImpl(aMediaSource);
  MutexAutoLock lock(self->mMutex);

  RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
      workerPrivate, "MediaSourceHandleImpl", [self]() { self->Destroy(); });
  if (NS_WARN_IF(!workerRef)) {
    return nullptr;
  }

  self->mWorkerRef = new ThreadSafeWorkerRef(workerRef);
  return self.forget();
}

MediaSourceHandleImpl::MediaSourceHandleImpl(MediaSource* aMediaSource)
    : mMutex("MediaSourceHandleImpl"), mMediaSource(aMediaSource) {}

MediaSourceHandleImpl::~MediaSourceHandleImpl() { Destroy(); }

void MediaSourceHandleImpl::Destroy() {
  MutexAutoLock lock(mMutex);
  if (!mWorkerRef) {
    MOZ_ASSERT(!mMediaSource);
    return;
  }

  if (mWorkerRef->Private()->IsOnCurrentThread()) {
    mMediaSource = nullptr;
    mWorkerRef = nullptr;
    return;
  }

  class DestroyRunnable final : public WorkerRunnable {
    RefPtr<ThreadSafeWorkerRef> mWorkerRef;
    RefPtr<MediaSource> mMediaSource;

   public:
    DestroyRunnable(RefPtr<ThreadSafeWorkerRef>&& aWorkerRef,
                    RefPtr<MediaSource>&& aMediaSource)
        : WorkerRunnable(aWorkerRef->Private()),
          mWorkerRef(std::move(aWorkerRef)),
          mMediaSource(std::move(aMediaSource)) {}

    bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
      mMediaSource = nullptr;
      return true;
    }
  };

  auto runnable = MakeRefPtr<DestroyRunnable>(std::move(mWorkerRef),
                                              std::move(mMediaSource));
  runnable->Dispatch();
}

}  // namespace mozilla::dom
