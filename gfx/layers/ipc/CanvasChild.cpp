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
#include "mozilla/layers/ImageDataSerializer.h"
#include "nsIObserverService.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla {
namespace layers {

/* static */ bool CanvasChild::mInForeground = true;

class RingBufferWriterServices final
    : public CanvasEventRingBuffer::WriterServices {
 public:
  explicit RingBufferWriterServices(RefPtr<CanvasChild> aCanvasChild)
      : mCanvasChild(aCanvasChild) {}

  ~RingBufferWriterServices() override = default;

  void Destroy() override { mCanvasChild = nullptr; }

  bool ReaderClosed() override {
    if (!mCanvasChild) {
      return false;
    }
    return !mCanvasChild->CanSend() || ipc::ProcessChild::ExpectingShutdown();
  }

  void ResumeReader() override {
    if (!mCanvasChild) {
      return;
    }
    mCanvasChild->ResumeTranslation();
  }

 private:
  WeakPtr<CanvasChild> mCanvasChild;
};

CanvasChild::CanvasChild(dom::ThreadSafeWorkerRef* aWorkerRef)
    : mMutex("CanvasChild::mMutex"),
      mWorkerRef(aWorkerRef),
      mIsOnWorker(!!aWorkerRef){};

CanvasChild::~CanvasChild() { NS_ASSERT_OWNINGTHREAD(CanvasChild); }

static void NotifyCanvasDeviceReset() {
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

ipc::IPCResult CanvasChild::RecvBlockCanvas() {
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->BlockCanvas();
  }
  return IPC_OK();
}

RefPtr<CanvasDrawEventRecorder> CanvasChild::EnsureRecorder(
    TextureType aTextureType) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mRecorder) {
    MOZ_ASSERT(mTextureType == TextureType::Unknown);

    {
      MutexAutoLock lock(mMutex);
      mRecorder = MakeAndAddRef<CanvasDrawEventRecorder>(mWorkerRef);
    }

    mTextureType = aTextureType;
    SharedMemoryBasic::Handle handle;
    CrossProcessSemaphoreHandle readerSem;
    CrossProcessSemaphoreHandle writerSem;
    if (!mRecorder->Init(OtherPid(), &handle, &readerSem, &writerSem,
                         MakeUnique<RingBufferWriterServices>(this))) {
      mRecorder = nullptr;
      return nullptr;
    }

    if (CanSend()) {
      gfx::BackendType backendType =
          gfxPlatform::GetPlatform()->GetPreferredCanvasBackend();
      Unused << SendInitTranslator(mTextureType, backendType, std::move(handle),
                                   std::move(readerSem), std::move(writerSem),
                                   /* aUseIPDLThread */ false);
    }
  }

  MOZ_RELEASE_ASSERT(mTextureType == aTextureType,
                     "We only support one remote TextureType currently.");
  return mRecorder;
}

void CanvasChild::ActorDestroy(ActorDestroyReason aWhy) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // Explicitly drop our reference to the recorder, because it holds a reference
  // to us via the ResumeTranslation callback.
  if (mRecorder) {
    mRecorder->DetachResources();
    mRecorder = nullptr;
  }

  MutexAutoLock lock(mMutex);
  mWorkerRef = nullptr;
}

void CanvasChild::ResumeTranslation() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (CanSend()) {
    SendResumeTranslation();
  }
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

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

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

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

  // We're forwarding textures, so we must be in the foreground.
  mInForeground = true;

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

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return false;
  }

  if (!mIsInTransaction) {
    // Ensure we are using a large buffer when in the foreground and small one
    // in the background.
    if (mInForeground != mRecorder->UsingLargeStream()) {
      SharedMemoryBasic::Handle handle;
      if (!mRecorder->SwitchBuffer(OtherPid(), &handle) ||
          !SendNewBuffer(std::move(handle))) {
        mRecorder = nullptr;
        return false;
      }
    }

    mRecorder->RecordEvent(RecordedCanvasBeginTransaction());
    mIsInTransaction = true;
  }

  return true;
}

void CanvasChild::EndTransaction() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

  if (mIsInTransaction) {
    mRecorder->RecordEvent(RecordedCanvasEndTransaction());
    mIsInTransaction = false;
  }

  ++mTransactionsSinceGetDataSurface;
}

/* static */
void CanvasChild::ClearCachedResources() {
  // We use this as a proxy for the tab being in the backgound.
  mInForeground = false;
}

bool CanvasChild::ShouldBeCleanedUp() const {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // Always return true if we've been deactivated.
  if (Deactivated()) {
    return true;
  }

  // We can only be cleaned up if nothing else references our recorder.
  return !mRecorder || mRecorder->hasOneRef();
}

already_AddRefed<gfx::DrawTarget> CanvasChild::CreateDrawTarget(
    int64_t aTextureId, gfx::IntSize aSize, gfx::SurfaceFormat aFormat) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return nullptr;
  }

  RefPtr<gfx::DrawTarget> dummyDt = gfx::Factory::CreateDrawTarget(
      gfx::BackendType::SKIA, gfx::IntSize(1, 1), aFormat);
  RefPtr<gfx::DrawTarget> dt = MakeAndAddRef<gfx::DrawTargetRecording>(
      mRecorder, dummyDt, gfx::IntRect(gfx::IntPoint(0, 0), aSize));

  mTextureInfo.insert({aTextureId, {}});

  return dt.forget();
}

void CanvasChild::RecordEvent(const gfx::RecordedEvent& aEvent) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

  mRecorder->RecordEvent(aEvent);
}

already_AddRefed<gfx::DataSourceSurface> CanvasChild::GetDataSurface(
    int64_t aTextureId, const gfx::SourceSurface* aSurface, bool aDetached) {
  gfx::IntSize ssSize = aSurface->GetSize();
  gfx::SurfaceFormat ssFormat = aSurface->GetFormat();
  int32_t dataStride =
      gfx::GetAlignedStride<4>(ssSize.width, BytesPerPixel(ssFormat));
  RefPtr<gfx::DataSourceSurface> dataSurface =
      gfx::Factory::CreateDataSourceSurfaceWithStride(ssSize, ssFormat,
                                                      dataStride);
  if (!dataSurface) {
    gfxWarning() << "Failed to create DataSourceSurface.";
    return nullptr;
  }

  if (!ReadInto(aTextureId, aSurface, dataSurface, aSurface->GetRect(),
                aDetached)) {
    return nullptr;
  }

  return dataSurface.forget();
}

bool CanvasChild::ReadInto(int64_t aTextureId,
                           const gfx::SourceSurface* aSurface,
                           gfx::DataSourceSurface* aDataSurface,
                           const gfx::IntRect& aRect, bool aDetached) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  MOZ_ASSERT(aSurface);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return false;
  }

  // mTransactionsSinceGetDataSurface is used to determine if we want to prepare
  // a DataSourceSurface in the GPU process up front at the end of the
  // transaction, but that only makes sense if the canvas JS is requesting data
  // in between transactions.
  if (!mIsInTransaction) {
    mTransactionsSinceGetDataSurface = 0;
  }

  if (!EnsureBeginTransaction()) {
    return false;
  }

  // Shmem is only valid if the surface is the latest snapshot (not detached).
  if (!aDetached) {
    // If there is a shmem associated with this snapshot id, then we want to try
    // read directly from the shmem contents into the destination buffer without
    // going through the event ringbuffer.
    auto it = mTextureInfo.find(aTextureId);
    if (it != mTextureInfo.end() && it->second.mSnapshotShmem.IsReadable()) {
      ipc::Shmem& shmem = it->second.mSnapshotShmem;
      mRecorder->RecordEvent(RecordedPrepareShmem(aTextureId));
      uint32_t checkpoint = mRecorder->CreateCheckpoint();
      if (!mRecorder->WaitForCheckpoint(checkpoint)) {
        gfxWarning() << "Timed out preparing Shmem.";
        return true;
      }
      gfx::DataSourceSurface::ScopedMap map(aDataSurface,
                                            gfx::DataSourceSurface::READ_WRITE);
      if (!map.IsMapped()) {
        gfxWarning() << "Failed mapping DataSourceSurface.";
        return false;
      }
      gfx::IntSize size = aSurface->GetSize();
      gfx::SurfaceFormat format = aSurface->GetFormat();
      auto bpp = BytesPerPixel(format);
      auto stride = ImageDataSerializer::ComputeRGBStride(format, size.width);
      const uint8_t* srcPtr =
          shmem.get<uint8_t>() + aRect.y * stride + aRect.x * bpp;
      uint8_t* dstPtr = map.GetData();
      for (int32_t y = 0; y < aRect.height; y++) {
        memcpy(dstPtr, srcPtr, bpp * aRect.width);
        srcPtr += stride;
        dstPtr += map.GetStride();
      }
      return true;
    }
  }

  mRecorder->RecordEvent(RecordedPrepareDataForSurface(aSurface));
  uint32_t checkpoint = mRecorder->CreateCheckpoint();

  gfx::DataSourceSurface::ScopedMap map(aDataSurface,
                                        gfx::DataSourceSurface::READ_WRITE);
  if (!mRecorder->WaitForCheckpoint(checkpoint)) {
    gfxWarning() << "Timed out preparing data for DataSourceSurface.";
    return true;
  }

  if (!map.IsMapped()) {
    gfxWarning() << "Failed mapping DataSourceSurface.";
    return false;
  }

  mRecorder->RecordEvent(RecordedGetDataForSurface(aSurface, aRect));
  mRecorder->ReturnRead(map.GetData(), aDataSurface->GetSize(),
                        BytesPerPixel(aDataSurface->GetFormat()),
                        map.GetStride());

  return true;
}

already_AddRefed<gfx::SourceSurface> CanvasChild::WrapSurface(
    const RefPtr<gfx::SourceSurface>& aSurface, int64_t aTextureId) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  MOZ_ASSERT(aSurface);
  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return nullptr;
  }

  auto wrapper = MakeRefPtr<SourceSurfaceCanvasRecording>(aTextureId, aSurface,
                                                          this, mRecorder);
  wrapper->Init();
  return wrapper.forget();
}

void CanvasChild::DetachSurface(const RefPtr<gfx::SourceSurface>& aSurface) {
  if (auto* surface =
          static_cast<SourceSurfaceCanvasRecording*>(aSurface.get())) {
    surface->DrawTargetWillChange();
  }
}

ipc::IPCResult CanvasChild::RecvNotifyRequiresRefresh(int64_t aTextureId) {
  auto it = mTextureInfo.find(aTextureId);
  if (it != mTextureInfo.end()) {
    it->second.mRequiresRefresh = true;
  }
  return IPC_OK();
}

bool CanvasChild::RequiresRefresh(int64_t aTextureId) const {
  auto it = mTextureInfo.find(aTextureId);
  if (it != mTextureInfo.end()) {
    return it->second.mRequiresRefresh;
  }
  return false;
}

ipc::IPCResult CanvasChild::RecvSnapshotShmem(
    int64_t aTextureId, Shmem&& aShmem, SnapshotShmemResolver&& aResolve) {
  auto it = mTextureInfo.find(aTextureId);
  if (it != mTextureInfo.end()) {
    it->second.mSnapshotShmem = std::move(aShmem);
    aResolve(true);
  } else {
    aResolve(false);
  }
  return IPC_OK();
}

void CanvasChild::CleanupTexture(int64_t aTextureId) {
  mTextureInfo.erase(aTextureId);
}

}  // namespace layers
}  // namespace mozilla
