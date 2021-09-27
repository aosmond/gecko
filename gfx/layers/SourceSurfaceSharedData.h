/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_
#define MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_

#include "mozilla/gfx/2D.h"
#include "mozilla/Mutex.h"
#include "mozilla/ipc/SharedMemoryBasic.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "nsExpirationTracker.h"

namespace mozilla {
namespace layers {
class RenderRootStateManager;
}

namespace wr {
class IpcResourceUpdateQueue;
}

namespace gfx {

class SourceSurfaceSharedData;

/**
 * This class is used to wrap shared (as in process) data buffers allocated by
 * a SourceSurfaceSharedData object. It may live in the same process or a
 * different process from the actual SourceSurfaceSharedData object.
 *
 * If it is in the same process, mBuf is the same object as that in the surface.
 * It is a useful abstraction over just using the surface directly, because it
 * can have a different lifetime from the surface; if the surface gets freed,
 * consumers may continue accessing the data in the buffer. Releasing the
 * original surface is a signal which feeds into SharedSurfacesParent to decide
 * to release the SourceSurfaceSharedDataWrapper.
 *
 * If it is in a different process, mBuf is a new SharedMemoryBasic object which
 * mapped in the given shared memory handle as read only memory.
 */
class SourceSurfaceSharedDataWrapper final : public DataSourceSurface {
  typedef mozilla::ipc::SharedMemoryBasic SharedMemoryBasic;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceSharedDataWrapper,
                                          override)

  SourceSurfaceSharedDataWrapper()
      : mStride(0),
        mConsumers(0),
        mFormat(SurfaceFormat::UNKNOWN),
        mCreatorPid(0),
        mCreatorRef(true) {}

  void Init(const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat,
            const SharedMemoryBasic::Handle& aHandle,
            base::ProcessId aCreatorPid);

  void Init(SourceSurfaceSharedData* aSurface);

  base::ProcessId GetCreatorPid() const { return mCreatorPid; }

  int32_t Stride() override { return mStride; }

  SurfaceType GetType() const override { return SurfaceType::DATA; }
  IntSize GetSize() const override { return mSize; }
  SurfaceFormat GetFormat() const override { return mFormat; }

  uint8_t* GetData() override { return static_cast<uint8_t*>(mBuf->memory()); }

  bool OnHeap() const override { return false; }

  bool Map(MapType, MappedSurface* aMappedSurface) final;

  void Unmap() final;

  void ExpireMap();

  bool AddConsumer() { return ++mConsumers == 1; }

  bool RemoveConsumer(bool aForCreator) {
    MOZ_ASSERT(mConsumers > 0);
    if (aForCreator) {
      if (!mCreatorRef) {
        MOZ_ASSERT_UNREACHABLE("Already released creator reference!");
        return false;
      }
      mCreatorRef = false;
    }
    return --mConsumers == 0;
  }

  uint32_t GetConsumers() const {
    MOZ_ASSERT(mConsumers > 0);
    return mConsumers;
  }

  bool HasCreatorRef() const { return mCreatorRef; }

  nsExpirationState* GetExpirationState() { return &mExpirationState; }

 private:
  size_t GetDataLength() const {
    return static_cast<size_t>(mStride) * mSize.height;
  }

  size_t GetAlignedDataLength() const {
    return mozilla::ipc::SharedMemory::PageAlignedSize(GetDataLength());
  }

  bool EnsureMapped(size_t aLength);

  // Protects mapping and unmapping of mBuf.
  Maybe<Mutex> mHandleLock;
  nsExpirationState mExpirationState;
  int32_t mStride;
  uint32_t mConsumers;
  IntSize mSize;
  RefPtr<SharedMemoryBasic> mBuf;
  SurfaceFormat mFormat;
  base::ProcessId mCreatorPid;
  bool mCreatorRef;
};

/**
 * This class is used to wrap shared (as in process) data buffers used by a
 * source surface.
 */
class SourceSurfaceSharedData : public DataSourceSurface {
  typedef mozilla::ipc::SharedMemoryBasic SharedMemoryBasic;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceSharedData, override)

  SourceSurfaceSharedData()
      : mMutex("SourceSurfaceSharedData"),
        mStride(0),
        mFormat(SurfaceFormat::UNKNOWN) {}

  /**
   * Initialize the surface by creating a shared memory buffer with a size
   * determined by aSize, aStride and aFormat. It will create an external
   * image ID which may be used to access the surface in the compositor
   * process.
   */
  bool Init(const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat);

  Maybe<wr::ImageKey> UpdateKey(layers::RenderRootStateManager* aManager,
                                wr::IpcResourceUpdateQueue& aResources);

  Maybe<wr::ExternalImageId> GetExternalImageId() {
    if (!IsValid()) {
      return Nothing();
    }
    return Some(mId);
  }

  uint8_t* GetData() final {
    return static_cast<uint8_t*>(mBuf->memory());
  }

  int32_t Stride() final { return mStride; }

  SurfaceType GetType() const override { return SurfaceType::DATA_SHARED; }
  IntSize GetSize() const final { return mSize; }
  SurfaceFormat GetFormat() const final { return mFormat; }

  void SizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                           SizeOfInfo& aInfo) const final;
  bool IsValid() const final;

  bool OnHeap() const final { return false; }

  bool Map(MapType, MappedSurface* aMappedSurface) final = default;
  void Unmap() final = default;

  /**
   * Signals we have finished writing to the buffer and it may be marked as
   * read only.
   */
  void Finalize();

  /**
   * Increment the invalidation counter.
   */
  void Invalidate(const IntRect& aDirtyRect) final;

 protected:
  ~SourceSurfaceSharedData() override;

 private:
  friend class SourceSurfaceSharedDataWrapper;

  /**
   * Get a handle to share to another process for this buffer. Returns:
   *   NS_OK -- success, aHandle is valid.
   *   NS_ERROR_NOT_AVAILABLE -- handle was closed, need to reallocate.
   *   NS_ERROR_FAILURE -- failed to create a handle to share.
   */
  nsresult ShareToProcess(base::ProcessId aPid,
                          SharedMemoryBasic::Handle& aHandle);

  void CloseHandleInternal();

  size_t GetDataLength() const {
    return static_cast<size_t>(mStride) * mSize.height;
  }

  size_t GetAlignedDataLength() const {
    return mozilla::ipc::SharedMemory::PageAlignedSize(GetDataLength());
  }

  /**
   * Helper class that tracks the WebRender bindings for specific manager
   * instance.
   */
  class ImageKeyData final {
   public:
    ImageKeyData(layers::RenderRootStateManager* aManager,
                 const wr::ImageKey& aImageKey);
    ~ImageKeyData();

    ImageKeyData(ImageKeyData&& aOther);
    ImageKeyData& operator=(ImageKeyData&& aOther);
    ImageKeyData(const ImageKeyData&) = delete;
    ImageKeyData& operator=(const ImageKeyData&) = delete;

    void MergeDirtyRect(const Maybe<gfx::IntRect>& aDirtyRect);

    Maybe<gfx::IntRect> TakeDirtyRect() { return std::move(mDirtyRect); }

    RefPtr<layers::RenderRootStateManager> mManager;
    Maybe<gfx::IntRect> mDirtyRect;
    wr::ImageKey mImageKey;
  };

  static void Unshare(const wr::ExternalImageId& aId,
                      nsTArray<ImageKeyData>& aKeys);

  mutable Mutex mMutex;
  RefPtr<SharedMemoryBasic> mBuf;
  AutoTArray<ImageKeyData, 1> mKeys;
  wr::ExternalImageId mId;
  IntSize mSize;
  int32_t mStride;
  SurfaceFormat mFormat;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_ */
