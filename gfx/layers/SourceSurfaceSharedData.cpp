/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SourceSurfaceSharedData.h"

#include "mozilla/Likely.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/Types.h"  // for decltype
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "nsDebug.h"  // for NS_ABORT_OOM

#include "base/process_util.h"

#ifdef DEBUG
/**
 * If defined, this makes SourceSurfaceSharedData::Finalize memory protect the
 * underlying shared buffer in the producing process (the content or UI
 * process). Given flushing the page table is expensive, and its utility is
 * predominantly diagnostic (in case of overrun), turn it off by default.
 */
#  define SHARED_SURFACE_PROTECT_FINALIZED
#endif

using namespace mozilla::layers;

namespace mozilla {
namespace gfx {

void SourceSurfaceSharedDataWrapper::Init(
    const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat,
    const SharedMemoryBasic::Handle& aHandle, base::ProcessId aCreatorPid) {
  MOZ_ASSERT(!mBuf);
  mSize = aSize;
  mStride = aStride;
  mFormat = aFormat;
  mCreatorPid = aCreatorPid;

  size_t len = GetAlignedDataLength();
  mBuf = MakeAndAddRef<SharedMemoryBasic>();
  if (!mBuf->SetHandle(aHandle, ipc::SharedMemory::RightsReadOnly)) {
    MOZ_CRASH("Invalid shared memory handle!");
  }

  bool mapped = EnsureMapped(len);
  if ((sizeof(uintptr_t) <= 4 ||
       StaticPrefs::image_mem_shared_unmap_force_enabled_AtStartup()) &&
      len / 1024 >
          StaticPrefs::image_mem_shared_unmap_min_threshold_kb_AtStartup()) {
    mHandleLock.emplace("SourceSurfaceSharedDataWrapper::mHandleLock");

    if (mapped) {
      // Tracking at the initial mapping, and not just after the first use of
      // the surface means we might get unmapped again before the next frame
      // gets rendered if a low virtual memory condition persists.
      SharedSurfacesParent::AddTracking(this);
    }
  } else if (!mapped) {
    // We don't support unmapping for this surface, and we failed to map it.
    NS_ABORT_OOM(len);
  } else {
    mBuf->CloseHandle();
  }
}

void SourceSurfaceSharedDataWrapper::Init(SourceSurfaceSharedData* aSurface) {
  MOZ_ASSERT(!mBuf);
  MOZ_ASSERT(aSurface);
  mSize = aSurface->mSize;
  mStride = aSurface->mStride;
  mFormat = aSurface->mFormat;
  mCreatorPid = base::GetCurrentProcId();
  mBuf = aSurface->mBuf;
}

bool SourceSurfaceSharedDataWrapper::EnsureMapped(size_t aLength) {
  MOZ_ASSERT(!GetData());

  while (!mBuf->Map(aLength)) {
    nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>> expired;
    if (!SharedSurfacesParent::AgeOneGeneration(expired)) {
      return false;
    }
    MOZ_ASSERT(!expired.Contains(this));
    SharedSurfacesParent::ExpireMap(expired);
  }

  return true;
}

bool SourceSurfaceSharedDataWrapper::Map(MapType,
                                         MappedSurface* aMappedSurface) {
  uint8_t* dataPtr;

  if (mHandleLock) {
    MutexAutoLock lock(*mHandleLock);
    dataPtr = GetData();
    if (mMapCount == 0) {
      SharedSurfacesParent::RemoveTracking(this);
      if (!dataPtr) {
        size_t len = GetAlignedDataLength();
        if (!EnsureMapped(len)) {
          NS_ABORT_OOM(len);
        }
        dataPtr = GetData();
      }
    }
    ++mMapCount;
  } else {
    dataPtr = GetData();
    ++mMapCount;
  }

  MOZ_ASSERT(dataPtr);
  aMappedSurface->mData = dataPtr;
  aMappedSurface->mStride = mStride;
  return true;
}

void SourceSurfaceSharedDataWrapper::Unmap() {
  if (mHandleLock) {
    MutexAutoLock lock(*mHandleLock);
    if (--mMapCount == 0) {
      SharedSurfacesParent::AddTracking(this);
    }
  } else {
    --mMapCount;
  }
  MOZ_ASSERT(mMapCount >= 0);
}

void SourceSurfaceSharedDataWrapper::ExpireMap() {
  MutexAutoLock lock(*mHandleLock);
  if (mMapCount == 0) {
    mBuf->Unmap();
  }
}

SourceSurfaceSharedData::ImageKeyData::ImageKeyData(
    RenderRootStateManager* aManager, const wr::ImageKey& aImageKey)
    : mManager(aManager), mImageKey(aImageKey) {}

SourceSurfaceSharedData::ImageKeyData::ImageKeyData(
    SourceSurfaceSharedData::ImageKeyData&& aOther)
    : mManager(std::move(aOther.mManager)),
      mDirtyRect(std::move(aOther.mDirtyRect)),
      mImageKey(aOther.mImageKey) {}

SourceSurfaceSharedData::ImageKeyData&
SourceSurfaceSharedData::ImageKeyData::operator=(
    SourceSurfaceSharedData::ImageKeyData&& aOther) {
  mManager = std::move(aOther.mManager);
  mDirtyRect = std::move(aOther.mDirtyRect);
  mImageKey = aOther.mImageKey;
  return *this;
}

SourceSurfaceSharedData::ImageKeyData::~ImageKeyData() = default;

void SourceSurfaceSharedData::ImageKeyData::MergeDirtyRect(
    const Maybe<IntRect>& aDirtyRect) {
  if (mDirtyRect) {
    if (aDirtyRect) {
      mDirtyRect->UnionRect(mDirtyRect.ref(), aDirtyRect.ref());
    }
  } else {
    mDirtyRect = aDirtyRect;
  }
}

/* static */
void SourceSurfaceSharedData::Unshare(const wr::ExternalImageId& aId,
                                      nsTArray<ImageKeyData>& aKeys) {
  MOZ_ASSERT(NS_IsMainThread());

  for (const auto& entry : aKeys) {
    if (!entry.mManager->IsDestroyed()) {
      entry.mManager->AddImageKeyForDiscard(entry.mImageKey);
    }
  }

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (MOZ_UNLIKELY(!manager || !manager->CanSend())) {
    return;
  }

  if (manager->OwnsExternalImageId(aId)) {
    // Only attempt to release current mappings in the compositor process. It is
    // possible we had a surface that was previously shared, the compositor
    // process crashed / was restarted, and then we freed the surface. In that
    // case we know the mapping has already been freed.
    manager->SendRemoveSharedSurface(aId);
  }
}

SourceSurfaceSharedData::~SourceSurfaceSharedData() {
  if (NS_IsMainThread()) {
    Unshare(mId, mKeys);
  } else {
    class UnshareHelper final : public Runnable {
     public:
      UnshareHelper(const wr::ExternalImageId& aId,
                    nsTArray<ImageKeyData>&& aKeys)
          : Runnable("SourceSurfaceSharedData::UnshareHelper"),
            mId(aId),
            mKeys(std::move(aKeys)) {}

      NS_IMETHOD Run() override {
        Unshare(mId, mKeys);
        return NS_OK;
      }

     private:
      wr::ExternalImageId mId;
      AutoTArray<ImageKeyData, 1> mKeys;
    };

    nsCOMPtr<nsIRunnable> runnable = new UnshareHelper(mId, std::move(mKeys));
    NS_DispatchToMainThread(runnable.forget());
  }
}

bool SourceSurfaceSharedData::Init(const IntSize& aSize, int32_t aStride,
                                   SurfaceFormat aFormat) {
  mSize = aSize;
  mStride = aStride;
  mFormat = aFormat;

  size_t len = GetAlignedDataLength();
  mBuf = new SharedMemoryBasic();
  if (NS_WARN_IF(!mBuf->Create(len)) || NS_WARN_IF(!mBuf->Map(len))) {
    mBuf = nullptr;
    return false;
  }

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (NS_WARN_IF(!manager)) {
    // We cannot try to share the surface, most likely because the GPU process
    // crashed. Ideally, we would retry when it is ready, but the handles may be
    // a scarce resource, which can cause much more serious problems if we run
    // out. We'll redecode into a fresh buffer later.
    mBuf->CloseHandle();
    return true;
  }

  mId = manager->GetNextExternalImageId();

  // If we live in the same process, then it is a simple matter of directly
  // asking the parent instance to store a pointer to the same data, no need
  // to map the data into our memory space twice.
  auto pid = manager->OtherPid();
  if (pid == base::GetCurrentProcId()) {
    SharedSurfacesParent::AddSameProcess(mId, this);
    mBuf->CloseHandle();
    return true;
  }

  // Attempt to share a handle with the GPU process. The handle may or may not
  // be available -- it will only be available if it is either not yet finalized
  // and/or if it has been finalized but never used for drawing in process.
  ipc::SharedMemoryBasic::Handle handle = ipc::SharedMemoryBasic::NULLHandle();
  bool shared = mBuf->ShareToProcess(pid, &handle);
  if (MOZ_UNLIKELY(!shared)) {
    return false;
  }

  mBuf->CloseHandle();
  manager->SendAddSharedSurface(
      mId, SurfaceDescriptorShared(mSize, mStride, mFormat, handle));

  return true;
}

Maybe<wr::ImageKey> SourceSurfaceSharedData::UpdateKey(
    RenderRootStateManager* aManager, wr::IpcResourceUpdateQueue& aResources) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(!aManager->IsDestroyed());

  if (!IsValid()) {
    return Nothing();
  }

  MutexAutoLock lock(mMutex);

  // We iterate through all of the items to ensure we clean up the old
  // RenderRootStateManager references. Most of the time there will be few
  // entries and this should not be particularly expensive compared to the
  // cost of duplicating image keys. In an ideal world, we would generate a
  // single key for the surface, and it would be usable on all of the
  // renderer instances. For now, we must allocate a key for each WR bridge.
  wr::ImageKey key;
  bool found = false;
  auto i = mKeys.Length();
  while (i > 0) {
    --i;
    ImageKeyData& entry = mKeys[i];
    if (entry.mManager->IsDestroyed()) {
      mKeys.RemoveElementAt(i);
    } else if (entry.mManager == aManager) {
      WebRenderBridgeChild* wrBridge = aManager->WrBridge();
      MOZ_ASSERT(wrBridge);

      // Even if the manager is the same, its underlying WebRenderBridgeChild
      // can change state. If our namespace differs, then our old key has
      // already been discarded.
      bool ownsKey = wrBridge->GetNamespace() == entry.mImageKey.mNamespace;
      if (!ownsKey) {
        entry.mImageKey = wrBridge->GetNextImageKey();
        entry.TakeDirtyRect();
        aResources.AddSharedExternalImage(mId, entry.mImageKey);
      } else {
        Maybe<IntRect> dirtyRect = entry.TakeDirtyRect();
        if (dirtyRect) {
          MOZ_ASSERT(mShared);
          aResources.UpdateSharedExternalImage(
              mId, entry.mImageKey, ViewAs<ImagePixel>(dirtyRect.ref()));
        }
      }

      key = entry.mImageKey;
      found = true;
    }
  }

  if (!found) {
    key = aManager->WrBridge()->GetNextImageKey();
    ImageKeyData data(aManager, key);
    mKeys.AppendElement(std::move(data));
    aResources.AddSharedExternalImage(mId, key);
  }

  return Some(key);
}

void SourceSurfaceSharedData::Invalidate(const IntRect& aDirtyRect) {
  MutexAutoLock lock(mMutex);
  for (auto& entry : mKeys) {
    entry.MergeDirtyRect(aDirtyRect);
  }
}

void SourceSurfaceSharedData::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                                  SizeOfInfo& aInfo) const {
  aInfo.AddType(SurfaceType::DATA_SHARED);
  aInfo.mNonHeapBytes = GetAlignedDataLength();
  aInfo.mExternalId = mId;
}

bool SourceSurfaceSharedData::IsValid() const {
  MOZ_ASSERT(NS_IsMainThread());
  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  return manager && manager->OwnsExternalImageId(mId);
}

void SourceSurfaceSharedData::Finalize() {
#ifdef SHARED_SURFACE_PROTECT_FINALIZED
  size_t len = GetAlignedDataLength();
  mBuf->Protect(static_cast<char*>(mBuf->memory()), len, RightsRead);
#endif
}

}  // namespace gfx
}  // namespace mozilla
