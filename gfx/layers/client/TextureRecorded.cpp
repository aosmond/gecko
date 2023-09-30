/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureRecorded.h"

#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla {
namespace layers {

// The texture ID is used in the GPU process both to lookup the real texture in
// the canvas threads and to lookup the SurfaceDescriptor for that texture in
// the compositor thread. It is therefore important that the ID is unique (per
// recording process), otherwise an old descriptor can be picked up. This means
// we can't use the pointer in the recording process as an ID like we do for
// other objects.
static int64_t sNextRecordedTextureId = 0;

RecordedTextureData::RecordedTextureData(gfx::IntSize aSize,
                                         gfx::SurfaceFormat aFormat)
    : mMutex("RecordedTextureData::mMutex"), mSize(aSize), mFormat(aFormat) {}

RecordedTextureData::~RecordedTextureData() = default;

bool RecordedTextureData::Init(TextureType aTextureType) {
  if (NS_WARN_IF(!NS_IsMainThread() && !dom::GetCurrentThreadWorkerPrivate())) {
    MOZ_ASSERT_UNREACHABLE(
        "RecordedTextureData must be created on main or DOM worker threads!");
    return false;
  }

  auto* cm = gfx::CanvasManagerChild::Get();
  if (NS_WARN_IF(!cm)) {
    return false;
  }

  RefPtr<CanvasChild> canvasChild = cm->GetCanvasChild();
  if (NS_WARN_IF(!canvasChild)) {
    return false;
  }

  RefPtr<CanvasDrawEventRecorder> recorder =
      canvasChild->EnsureRecorder(aTextureType);
  if (NS_WARN_IF(!recorder)) {
    return false;
  }

  recorder->TrackRecordedTexture(this);

  MutexAutoLock lock(mMutex);
  mRecorder = std::move(recorder);
  mCanvasChild = std::move(canvasChild);
  return true;
}

void RecordedTextureData::DestroyOnOwningThread() {
  MutexAutoLock lock(mMutex);

  // We need the translator to drop its reference for the DrawTarget first,
  // because the TextureData might need to destroy its DrawTarget within a lock.
  mSnapshot = nullptr;
  mDT = nullptr;
  if (mRecorder) {
    mRecorder->UntrackRecordedTexture(this);
    mRecorder = nullptr;
  }
  if (mCanvasChild) {
    mCanvasChild->RecordEvent(RecordedTextureDestruction(mTextureId));
    mCanvasChild = nullptr;
  }
}

void RecordedTextureData::Deallocate(LayersIPCChannel* aAllocator) {
  // When we are deallocating, we know this is our only reference, and this is
  // effectively the destructor. This will either happen on the main thread (for
  // main thread recording) or the ImageBridgeChild thread (for DOM worker
  // recordings). The only method we can race with is DestroyOnOwningThread
  // which may be called on the DOM worker thread during worker shutdown. As
  // such, the mutex is only necessary for these two methods.
  MutexAutoLock lock(mMutex);

  if (!mRecorder) {
    delete this;
    return;
  }

  mRecorder->AddPendingDeletion([self = this]() -> void {
    self->DestroyOnOwningThread();
    delete self;
  });
}

void RecordedTextureData::FillInfo(TextureData::Info& aInfo) const {
  aInfo.size = mSize;
  aInfo.format = mFormat;
  aInfo.supportsMoz2D = true;
  aInfo.hasSynchronization = true;
}

bool RecordedTextureData::Lock(OpenMode aMode) {
  if (NS_WARN_IF(!mCanvasChild)) {
    MOZ_ASSERT_UNREACHABLE("Lock after shutdown?");
    return false;
  }

  if (!mCanvasChild->EnsureBeginTransaction()) {
    return false;
  }

  if (!mDT) {
    mTextureId = sNextRecordedTextureId++;
    mCanvasChild->RecordEvent(RecordedNextTextureId(mTextureId));
    mDT = mCanvasChild->CreateDrawTarget(mSize, mFormat);
    if (!mDT) {
      return false;
    }

    // We lock the TextureData when we create it to get the remote DrawTarget.
    mCanvasChild->OnTextureWriteLock();
    mLockedMode = aMode;
    return true;
  }

  mCanvasChild->RecordEvent(RecordedTextureLock(mTextureId, aMode));
  if (aMode & OpenMode::OPEN_WRITE) {
    mCanvasChild->OnTextureWriteLock();
  }
  mLockedMode = aMode;
  return true;
}

void RecordedTextureData::Unlock() {
  if (NS_WARN_IF(mLockedMode == OpenMode::OPEN_NONE)) {
    return;
  }

  if (NS_WARN_IF(!mCanvasChild)) {
    return;
  }

  if ((mLockedMode == OpenMode::OPEN_READ_WRITE) &&
      mCanvasChild->ShouldCacheDataSurface()) {
    mSnapshot = mDT->Snapshot();
    mDT->DetachAllSnapshots();
    mCanvasChild->RecordEvent(RecordedCacheDataSurface(mSnapshot.get()));
  }

  mCanvasChild->RecordEvent(RecordedTextureUnlock(mTextureId));
  mLockedMode = OpenMode::OPEN_NONE;
}

already_AddRefed<gfx::DrawTarget> RecordedTextureData::BorrowDrawTarget() {
  mSnapshot = nullptr;
  return do_AddRef(mDT);
}

void RecordedTextureData::EndDraw() {
  if (NS_WARN_IF(!mCanvasChild)) {
    MOZ_ASSERT_UNREACHABLE("Draw interrupted by shutdown?");
    return;
  }

  MOZ_ASSERT(mDT->hasOneRef());
  MOZ_ASSERT(mLockedMode == OpenMode::OPEN_READ_WRITE);

  if (mCanvasChild->ShouldCacheDataSurface()) {
    mSnapshot = mDT->Snapshot();
    mCanvasChild->RecordEvent(RecordedCacheDataSurface(mSnapshot.get()));
  }
}

already_AddRefed<gfx::SourceSurface> RecordedTextureData::BorrowSnapshot() {
  // There are some failure scenarios where we have no DrawTarget and
  // BorrowSnapshot is called in an attempt to copy to a new texture.
  if (!mDT || !mCanvasChild) {
    return nullptr;
  }

  if (mSnapshot) {
    return mCanvasChild->WrapSurface(mSnapshot);
  }

  return mCanvasChild->WrapSurface(mDT->Snapshot());
}

bool RecordedTextureData::Serialize(SurfaceDescriptor& aDescriptor) {
  aDescriptor = SurfaceDescriptorRecorded(mTextureId);
  return true;
}

void RecordedTextureData::OnForwardedToHost() {
  if (mCanvasChild) {
    mCanvasChild->OnTextureForwarded();
  }
}

TextureFlags RecordedTextureData::GetTextureFlags() const {
  // With WebRender, resource open happens asynchronously on RenderThread.
  // Use WAIT_HOST_USAGE_END to keep TextureClient alive during host side usage.
  return TextureFlags::WAIT_HOST_USAGE_END | TextureFlags::DATA_SELF_DELETING;
}

}  // namespace layers
}  // namespace mozilla
