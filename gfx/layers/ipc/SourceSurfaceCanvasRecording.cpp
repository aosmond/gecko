/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SourceSurfaceCanvasRecording.h"

#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla::layers {

SourceSurfaceCanvasRecording::SourceSurfaceCanvasRecording(
    const RefPtr<gfx::SourceSurface>& aRecordedSuface,
    CanvasChild* aCanvasChild, const RefPtr<CanvasDrawEventRecorder>& aRecorder)
    : mMutex("SourceSurfaceCanvasRecording::mMutex"),
      mRecordedSurface(aRecordedSuface),
      mCanvasChild(aCanvasChild),
      mRecorder(aRecorder) {}

SourceSurfaceCanvasRecording::~SourceSurfaceCanvasRecording() {
  MutexAutoLock lock(mMutex);

  // We already got destroyed by the worker shutdown.
  if (!mRecorder) {
    return;
  }

  // We are on the owning thread, so we can destroy ourselves.
  if ((mWorkerRef && mWorkerRef->Private()->IsOnCurrentThread()) ||
      (!mWorkerRef && NS_IsMainThread())) {
    DestroyOnOwningThreadLocked();
    return;
  }

  // Otherwise we need to request the recorder to do it for us.
  ReferencePtr surfaceAlias = this;
  mRecorder->AddPendingDeletion(
      [recorder = std::move(mRecorder), surfaceAlias,
       aliasedSurface = std::move(mRecordedSurface),
       canvasChild = std::move(mCanvasChild)]() -> void {
        recorder->RemoveStoredObject(surfaceAlias);
        recorder->RecordEvent(RecordedRemoveSurfaceAlias(surfaceAlias));
      });
}

bool SourceSurfaceCanvasRecording::Init() {
  MOZ_ASSERT(mRecordedSurface);
  MOZ_ASSERT(mCanvasChild);
  MOZ_ASSERT(mRecorder);

  if (dom::WorkerPrivate* workerPrivate =
          dom::GetCurrentThreadWorkerPrivate()) {
    ThreadSafeWeakPtr<SourceSurfaceCanvasRecording> weakRef(this);
    RefPtr<dom::StrongWorkerRef> strongRef = dom::StrongWorkerRef::Create(
        workerPrivate, "SourceSurfaceCanvasRecording::Init",
        [weakRef = std::move(weakRef)]() mutable {
          RefPtr<SourceSurfaceCanvasRecording> self(weakRef);
          if (self) {
            self->DestroyOnOwningThread();
          }
        });
    if (NS_WARN_IF(!strongRef)) {
      return false;
    }

    MutexAutoLock lock(mMutex);
    mWorkerRef = new dom::ThreadSafeWorkerRef(strongRef);
  } else if (!NS_IsMainThread()) {
    MOZ_ASSERT_UNREACHABLE("Can only create on main or worker threads!");
    return false;
  }

  // It's important that AddStoredObject is called first because that will
  // run any pending processing required by recorded objects that have been
  // deleted off the main thread.
  mRecorder->AddStoredObject(this);
  mRecorder->RecordEvent(RecordedAddSurfaceAlias(this, mRecordedSurface));
  return true;
}

void SourceSurfaceCanvasRecording::DestroyOnOwningThreadLocked() {
  if (mRecorder) {
    ReferencePtr surfaceAlias = this;
    mRecorder->RemoveStoredObject(surfaceAlias);
    mRecorder->RecordEvent(RecordedRemoveSurfaceAlias(surfaceAlias));
    mRecorder = nullptr;
  }

  mRecordedSurface = nullptr;
  mCanvasChild = nullptr;
  mWorkerRef = nullptr;
}

void SourceSurfaceCanvasRecording::DestroyOnOwningThread() {
  MutexAutoLock lock(mMutex);
  DestroyOnOwningThreadLocked();
}

already_AddRefed<gfx::DataSourceSurface>
SourceSurfaceCanvasRecording::GetDataSurface() {
  EnsureDataSurfaceOnOwningThread();
  return do_AddRef(mDataSourceSurface);
}

bool SourceSurfaceCanvasRecording::IsOnOwningThread() const {
  MutexAutoLock lock(mMutex);
  if (mWorkerRef) {
    return mWorkerRef->Private()->IsOnCurrentThread();
  }
  return NS_IsMainThread();
}

void SourceSurfaceCanvasRecording::EnsureDataSurfaceOnOwningThread() {
  MutexAutoLock lock(mMutex);
  if (mDataSourceSurface || !mRecorder) {
    return;
  }

  // We don't have the surface but we are on the right thread to get it.
  if ((mWorkerRef && mWorkerRef->Private()->IsOnCurrentThread()) ||
      (!mWorkerRef && NS_IsMainThread())) {
    mDataSourceSurface = mCanvasChild->GetDataSurface(mRecordedSurface);
    return;
  }

  SynchronousTask task("SourceSurfaceCanvasRecording::EnsureDataSurfaceOnOwningThread");
  if (!mWorkerRef) {
    NS_DispatchToMainThread(NS_NewRunnable("SourceSurfaceCanvasRecording::EnsureDataSurfaceOnOwningThread", [&]() {
			    AutoCompleteTask complete(&task);
			    if (!mDataSourceSurface) {
    mDataSourceSurface = mCanvasChild->GetDataSurface(mRecordedSurface);
    }
			    }));
    task.Wait();
    return;
  }

  class EnsureDataSurfaceRunnable final : public dom::WorkerRunnable {
    EnsureDataSurfaceRunnable(dom::WorkerPrivate* aWorkerPrivate, RefPtr<gfx::DataSourceSurface>* aSurface, CanvasChild* aCanvasChild) : dom::WorkerRunnable(aWorkerPrivate), mSurface(aSurface), mCanvasChild(aCanvasChild) {}

    bool WorkerRun(JSContext*, dom::WorkerPrivate*) override {
      if (!*mSurface) {
	      *mSurface = mCanvasChild->GetDataSurface(mRecordedSurface);
      }
    }

    RefPtr<gfx::DataSourceSurface>* mSurface;
    CanvasChild* mCanvasChild;
  }
}

}  // namespace mozilla::layers
