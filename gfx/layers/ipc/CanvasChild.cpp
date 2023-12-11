/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasChild.h"

#include "MainThreadUtils.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/gfx/DrawTargetRecording.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/Maybe.h"
#include "nsIObserverService.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla {
namespace layers {

class RecorderHelpers final : public CanvasDrawEventRecorder::Helpers {
 public:
  explicit RecorderHelpers(const RefPtr<CanvasChild>& aCanvasChild)
      : mCanvasChild(aCanvasChild) {}

  ~RecorderHelpers() override = default;

  bool InitTranslator(const TextureType& aTextureType, Handle&& aReadHandle,
                      nsTArray<Handle>&& aBufferHandles,
                      const uint64_t& aBufferSize,
                      CrossProcessSemaphoreHandle&& aReaderSem,
                      CrossProcessSemaphoreHandle&& aWriterSem,
                      const bool& aUseIPDLThread) override {
    if (!mCanvasChild) {
      return false;
    }
    return mCanvasChild->SendInitTranslator(
        aTextureType, std::move(aReadHandle), std::move(aBufferHandles),
        aBufferSize, std::move(aReaderSem), std::move(aWriterSem),
        aUseIPDLThread);
  }

  bool AddBuffer(Handle&& aBufferHandle, const uint64_t& aBufferSize) override {
    if (!mCanvasChild) {
      return false;
    }
    return mCanvasChild->SendAddBuffer(std::move(aBufferHandle), aBufferSize);
  }

  void Destroy() override { mCanvasChild = nullptr; }

  bool ReaderClosed() override {
    if (!mCanvasChild) {
      return false;
    }
    return !mCanvasChild->CanSend() || ipc::ProcessChild::ExpectingShutdown();
  }

  bool RestartReader() override {
    if (!mCanvasChild) {
      return false;
    }
    return mCanvasChild->SendRestartTranslation();
  }

 private:
  WeakPtr<CanvasChild> mCanvasChild;
};

CanvasChild::CanvasChild(dom::ThreadSafeWorkerRef* aWorkerRef)
    : mMutex("CanvasChild::mMutex"),
      mWorkerRef(aWorkerRef),
      mIsOnWorker(!!aWorkerRef){};

CanvasChild::~CanvasChild() = default;

static void NotifyCanvasDeviceReset() {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(
        NewRunnableFunction(__func__, &NotifyCanvasDeviceReset));
    return;
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "canvas-device-reset", nullptr);
  }
}

ipc::IPCResult CanvasChild::RecvNotifyDeviceChanged() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  NotifyCanvasDeviceReset();
  mRecorder->RecordEvent(RecordedDeviceChangeAcknowledged());
  return IPC_OK();
}

/* static */ bool CanvasChild::mDeactivated = false;

ipc::IPCResult CanvasChild::RecvDeactivate() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  RefPtr<CanvasChild> self(this);
  mDeactivated = true;
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->DeactivateCanvas();
  }
  NotifyCanvasDeviceReset();
  return IPC_OK();
}

RefPtr<CanvasDrawEventRecorder> CanvasChild::EnsureRecorder(
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat, TextureType aTextureType) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mRecorder) {
    RefPtr<CanvasDrawEventRecorder> recorder;
    {
      MutexAutoLock lock(mMutex);
      recorder = MakeAndAddRef<CanvasDrawEventRecorder>(mWorkerRef);
    }

    if (!recorder->Init(aTextureType, MakeUnique<RecorderHelpers>(this))) {
      return nullptr;
    }

    mRecorder = recorder.forget();
  }

  MOZ_RELEASE_ASSERT(mRecorder->GetTextureType() == aTextureType,
                     "We only support one remote TextureType currently.");

  EnsureDataSurfaceShmem(aSize, aFormat);
  return mRecorder;
}

void CanvasChild::ActorDestroy(ActorDestroyReason aWhy) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (mRecorder) {
    mRecorder->DetachResources();
  }

  MutexAutoLock lock(mMutex);
  mWorkerRef = nullptr;
}

void CanvasChild::Destroy() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (CanSend()) {
    Send__delete__(this);
  }

  MutexAutoLock lock(mMutex);
  mWorkerRef = nullptr;
}

void CanvasChild::OnTextureWriteLock() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  mHasOutstandingWriteLock = true;
  mLastWriteLockCheckpoint = mRecorder->CreateCheckpoint();
}

void CanvasChild::OnTextureForwarded() {
  if (mIsOnWorker) {
    MutexAutoLock lock(mMutex);
    if (NS_WARN_IF(!mWorkerRef)) {
      // We have begun shutdown or destroyed the actor.
      return;
    }

    if (!mWorkerRef->Private()->IsOnCurrentThread()) {
      class ForwardedRunnable final : public dom::WorkerRunnable {
       public:
        ForwardedRunnable(dom::WorkerPrivate* aWorkerPrivate,
                          CanvasChild* aCanvasChild)
            : dom::WorkerRunnable(aWorkerPrivate), mCanvasChild(aCanvasChild) {}

        bool WorkerRun(JSContext*, dom::WorkerPrivate*) override {
          mCanvasChild->OnTextureForwarded();
          return true;
        }

       private:
        RefPtr<CanvasChild> mCanvasChild;
      };

      auto task = MakeRefPtr<ForwardedRunnable>(mWorkerRef->Private(), this);
      task->Dispatch();
      return;
    }
  } else if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "CanvasChild::OnTextureForwarded",
        [self = RefPtr{this}]() { self->OnTextureForwarded(); }));
    return;
  }

  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (mHasOutstandingWriteLock) {
    mRecorder->RecordEvent(RecordedCanvasFlush());
    if (!mRecorder->WaitForCheckpoint(mLastWriteLockCheckpoint)) {
      gfxWarning() << "Timed out waiting for last write lock to be processed.";
    }

    mHasOutstandingWriteLock = false;
  }

  // We hold onto the last transaction's external surfaces until we have waited
  // for the write locks in this transaction. This means we know that the
  // surfaces have been picked up in the canvas threads and there is no race
  // with them being removed from SharedSurfacesParent. Note this releases the
  // current contents of mLastTransactionExternalSurfaces.
  mRecorder->TakeExternalSurfaces(mLastTransactionExternalSurfaces);
}

bool CanvasChild::EnsureBeginTransaction() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mIsInTransaction) {
    RecordEvent(RecordedCanvasBeginTransaction());
    mIsInTransaction = true;
  }

  return true;
}

void CanvasChild::EndTransaction() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (mIsInTransaction) {
    RecordEvent(RecordedCanvasEndTransaction());
    mIsInTransaction = false;
    mDormant = false;
  } else {
    // Schedule to drop free buffers if we have no non-empty transactions.
    if (!mDormant) {
      mDormant = true;
      NS_DelayedDispatchToCurrentThread(
          NewRunnableMethod("CanvasChild::DropFreeBuffersWhenDormant", this,
                            &CanvasChild::DropFreeBuffersWhenDormant),
          StaticPrefs::gfx_canvas_remote_drop_buffer_milliseconds());
    }
  }

  ++mTransactionsSinceGetDataSurface;
}

void CanvasChild::DropFreeBuffersWhenDormant() {
  // Drop any free buffers if we have not had any non-empty transactions.
  if (mDormant) {
    mRecorder->DropFreeBuffers();
  }
}

void CanvasChild::ClearCachedResources() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  mRecorder->DropFreeBuffers();
}

bool CanvasChild::ShouldBeCleanedUp() const {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // Always return true if we've been deactivated.
  if (Deactivated()) {
    return true;
  }

  // We can only be cleaned up if nothing else references our recorder.
  return mRecorder->hasOneRef();
}

already_AddRefed<gfx::DrawTarget> CanvasChild::CreateDrawTarget(
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  RefPtr<gfx::DrawTarget> dummyDt = gfx::Factory::CreateDrawTarget(
      gfx::BackendType::SKIA, gfx::IntSize(1, 1), aFormat);
  RefPtr<gfx::DrawTarget> dt = MakeAndAddRef<gfx::DrawTargetRecording>(
      mRecorder, dummyDt, gfx::IntRect(gfx::IntPoint(0, 0), aSize));
  return dt.forget();
}

bool CanvasChild::EnsureDataSurfaceShmem(gfx::IntSize aSize,
                                         gfx::SurfaceFormat aFormat) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  size_t dataFormatWidth = aSize.width * BytesPerPixel(aFormat);
  size_t sizeRequired =
      ipc::SharedMemory::PageAlignedSize(dataFormatWidth * aSize.height);
  if (!mDataSurfaceShmemAvailable || mDataSurfaceShmem->Size() < sizeRequired) {
    RecordEvent(RecordedPauseTranslation());
    auto dataSurfaceShmem = MakeRefPtr<ipc::SharedMemoryBasic>();
    if (!dataSurfaceShmem->Create(sizeRequired) ||
        !dataSurfaceShmem->Map(sizeRequired)) {
      return false;
    }

    auto shmemHandle = dataSurfaceShmem->TakeHandle();
    if (!shmemHandle) {
      return false;
    }

    if (!SendSetDataSurfaceBuffer(std::move(shmemHandle), sizeRequired)) {
      return false;
    }

    mDataSurfaceShmem = dataSurfaceShmem.forget();
    mDataSurfaceShmemAvailable = true;
  }

  MOZ_ASSERT(mDataSurfaceShmemAvailable);
  return true;
}

void CanvasChild::RecordEvent(const gfx::RecordedEvent& aEvent) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

  mRecorder->RecordEvent(aEvent);
}

int64_t CanvasChild::CreateCheckpoint() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  return mRecorder->CreateCheckpoint();
}

already_AddRefed<gfx::DataSourceSurface> CanvasChild::GetDataSurface(
    const gfx::SourceSurface* aSurface) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  MOZ_ASSERT(aSurface);

  // mTransactionsSinceGetDataSurface is used to determine if we want to prepare
  // a DataSourceSurface in the GPU process up front at the end of the
  // transaction, but that only makes sense if the canvas JS is requesting data
  // in between transactions.
  if (!mIsInTransaction) {
    mTransactionsSinceGetDataSurface = 0;
  }

  if (!EnsureBeginTransaction()) {
    return nullptr;
  }

  RecordEvent(RecordedPrepareDataForSurface(aSurface));

  gfx::IntSize ssSize = aSurface->GetSize();
  gfx::SurfaceFormat ssFormat = aSurface->GetFormat();
  if (!EnsureDataSurfaceShmem(ssSize, ssFormat)) {
    return nullptr;
  }

  mDataSurfaceShmemAvailable = false;
  RecordEvent(RecordedGetDataForSurface(aSurface));
  auto checkpoint = CreateCheckpoint();
  struct DataShmemHolder {
    RefPtr<ipc::SharedMemoryBasic> shmem;
    RefPtr<CanvasChild> canvasChild;
  };

  auto* data = static_cast<uint8_t*>(mDataSurfaceShmem->memory());
  auto* closure = new DataShmemHolder{do_AddRef(mDataSurfaceShmem), this};
  auto dataFormatWidth = ssSize.width * BytesPerPixel(ssFormat);

  RefPtr<gfx::DataSourceSurface> dataSurface =
      gfx::Factory::CreateWrappingDataSourceSurface(
          data, dataFormatWidth, ssSize, ssFormat,
          [](void* aClosure) {
            auto* shmemHolder = static_cast<DataShmemHolder*>(aClosure);
            shmemHolder->canvasChild->ReturnDataSurfaceShmem(
                shmemHolder->shmem.forget());
            delete shmemHolder;
          },
          closure);

  mRecorder->WaitForCheckpoint(checkpoint);
  return dataSurface.forget();
}

already_AddRefed<gfx::SourceSurface> CanvasChild::WrapSurface(
    const RefPtr<gfx::SourceSurface>& aSurface) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!aSurface) {
    return nullptr;
  }

  auto wrapper =
      MakeRefPtr<SourceSurfaceCanvasRecording>(aSurface, this, mRecorder);
  wrapper->Init();
  return wrapper.forget();
}

void CanvasChild::ReturnDataSurfaceShmem(
    already_AddRefed<ipc::SharedMemoryBasic> aDataSurfaceShmem) {
  RefPtr<ipc::SharedMemoryBasic> data = aDataSurfaceShmem;
  // We can only reuse the latest data surface shmem.
  if (data == mDataSurfaceShmem) {
    MOZ_ASSERT(!mDataSurfaceShmemAvailable);
    mDataSurfaceShmemAvailable = true;
  }
}

}  // namespace layers
}  // namespace mozilla
