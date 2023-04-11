/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoi420FrameImpl.h"
#include "GMPSharedMemManager.h"
#include "GMPVideoHost.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/gmp/GMPTypes.h"
#include "mozilla/CheckedInt.h"

namespace mozilla::gmp {

GMPVideoi420FrameImpl::Plane GMPVideoi420FrameImpl::Plane::FromData(
    const GMPPlaneData& aData) {
  return {aData.mOffset(), aData.mSize(), aData.mStride()};
}

GMPPlaneData GMPVideoi420FrameImpl::Plane::ToData(
    const GMPVideoi420FrameImpl::Plane& aPlane) {
  return GMPPlaneData(aPlane.mOffset, aPlane.mSize, aPlane.mStride);
}

GMPVideoi420FrameImpl::GMPVideoi420FrameImpl(GMPVideoHostImpl* aHost)
    : mHost(aHost) {
  MOZ_ASSERT(aHost);
  mHost->DecodedFrameCreated(this);
}

GMPVideoi420FrameImpl::GMPVideoi420FrameImpl(
    const GMPVideoi420FrameData& aFrameData, GMPVideoHostImpl* aHost)
    : mHost(aHost),
      mBuffer(aFrameData.mBuffer()),
      mYPlane(Plane::FromData(aFrameData.mYPlane())),
      mUPlane(Plane::FromData(aFrameData.mUPlane())),
      mVPlane(Plane::FromData(aFrameData.mVPlane())),
      mWidth(aFrameData.mWidth()),
      mHeight(aFrameData.mHeight()),
      mTimestamp(aFrameData.mTimestamp()),
      mDuration(aFrameData.mDuration()) {
  MOZ_ASSERT(aHost);
  mHost->DecodedFrameCreated(this);
}

GMPVideoi420FrameImpl::~GMPVideoi420FrameImpl() {
  DestroyBuffer();
  if (mHost) {
    mHost->DecodedFrameDestroyed(this);
  }
}

void GMPVideoi420FrameImpl::DoneWithAPI() {
  DestroyBuffer();

  // Do this after destroying the buffer because destruction
  // involves deallocation, which requires a host.
  mHost = nullptr;
}

int32_t GMPVideoi420FrameImpl::AllocatedSize() const {
  if (mBuffer.IsWritable()) {
    return mBuffer.Size<uint8_t>();
  }
  return 0;
}

const uint8_t* GMPVideoi420FrameImpl::BufferPtr() const {
  return mBuffer.get<uint8_t>();
}

uint8_t* GMPVideoi420FrameImpl::BufferPtr() { return mBuffer.get<uint8_t>(); }

GMPErr GMPVideoi420FrameImpl::MaybeResize(int32_t aNewSize) {
  if (aNewSize <= AllocatedSize()) {
    return GMPNoErr;
  }

  if (!mHost) {
    return GMPGenericErr;
  }

  ipc::Shmem new_mem;
  if (!mHost->SharedMemMgr()->MgrAllocShmem(GMPSharedMem::kGMPFrameData,
                                            aNewSize, &new_mem) ||
      !new_mem.get<uint8_t>()) {
    return GMPAllocErr;
  }

  DestroyBuffer();

  mBuffer = new_mem;

  return GMPNoErr;
}

void GMPVideoi420FrameImpl::DestroyBuffer() {
  if (mHost && mBuffer.IsWritable()) {
    mHost->SharedMemMgr()->MgrDeallocShmem(GMPSharedMem::kGMPFrameData,
                                           mBuffer);
  }
  mBuffer = ipc::Shmem();
}

ipc::Shmem GMPVideoi420FrameImpl::TakeBuffer() {
  ipc::Shmem buffer = mBuffer;
  mBuffer = ipc::Shmem();
  return buffer;
}

bool GMPVideoi420FrameImpl::InitFrameData(GMPVideoi420FrameData& aFrameData) {
  aFrameData.mYPlane() = Plane::ToData(mYPlane);
  aFrameData.mUPlane() = Plane::ToData(mUPlane);
  aFrameData.mVPlane() = Plane::ToData(mVPlane);
  aFrameData.mBuffer() = mBuffer;
  aFrameData.mWidth() = mWidth;
  aFrameData.mHeight() = mHeight;
  aFrameData.mTimestamp() = mTimestamp;
  aFrameData.mDuration() = mDuration;

  // This method is called right before Shmem is sent to another process.
  // We need to effectively zero out our member copy so that we don't
  // try to delete memory we don't own later.
  mBuffer = ipc::Shmem();
  return true;
}

GMPVideoFrameFormat GMPVideoi420FrameImpl::GetFrameFormat() {
  return kGMPI420VideoFrame;
}

void GMPVideoi420FrameImpl::Destroy() { delete this; }

/* static */
bool GMPVideoi420FrameImpl::CheckFrameData(
    const GMPVideoi420FrameData& aFrameData) {
  // We may be passed the "wrong" shmem (one smaller than the actual size).
  // This implies a bug or serious error on the child size.  Ignore this frame
  // if so. Note: Size() greater than expected is also an error, but with no
  // negative consequences
  int32_t half_width = (aFrameData.mWidth() + 1) / 2;
  CheckedInt32 y_end = CheckedInt32(aFrameData.mYPlane().mOffset()) +
                       aFrameData.mYPlane().mSize();
  CheckedInt32 u_end = CheckedInt32(aFrameData.mUPlane().mOffset()) +
                       aFrameData.mUPlane().mSize();
  CheckedInt32 v_end = CheckedInt32(aFrameData.mVPlane().mOffset()) +
                       aFrameData.mVPlane().mSize();
  if ((aFrameData.mYPlane().mStride() <= 0) ||
      (aFrameData.mYPlane().mSize() <= 0) ||
      (aFrameData.mUPlane().mStride() <= 0) ||
      (aFrameData.mUPlane().mSize() <= 0) ||
      (aFrameData.mVPlane().mStride() <= 0) ||
      (aFrameData.mVPlane().mSize() <= 0) || (!y_end.isValid()) ||
      (!u_end.isValid()) || (!v_end.isValid()) ||
      (aFrameData.mYPlane().mOffset() < 0) ||
      (y_end.value() > aFrameData.mUPlane().mOffset()) ||
      (u_end.value() > aFrameData.mVPlane().mOffset()) ||
      (v_end.value() > (int32_t)aFrameData.mBuffer().Size<uint8_t>()) ||
      (aFrameData.mYPlane().mStride() < aFrameData.mWidth()) ||
      (aFrameData.mUPlane().mStride() < half_width) ||
      (aFrameData.mVPlane().mStride() < half_width) ||
      (aFrameData.mYPlane().mSize() <
       aFrameData.mYPlane().mStride() * aFrameData.mHeight()) ||
      (aFrameData.mUPlane().mSize() <
       aFrameData.mUPlane().mStride() * ((aFrameData.mHeight() + 1) / 2)) ||
      (aFrameData.mVPlane().mSize() <
       aFrameData.mVPlane().mStride() * ((aFrameData.mHeight() + 1) / 2))) {
    return false;
  }
  return true;
}

bool GMPVideoi420FrameImpl::CheckDimensions(int32_t aWidth, int32_t aHeight,
                                            int32_t aStride_y,
                                            int32_t aStride_u,
                                            int32_t aStride_v) {
  int32_t half_width = (aWidth + 1) / 2;
  if (aWidth < 1 || aHeight < 1 || aStride_y < aWidth ||
      aStride_u < half_width || aStride_v < half_width ||
      !(CheckedInt<int32_t>(aHeight) * aStride_y +
        ((CheckedInt<int32_t>(aHeight) + 1) / 2) *
            (CheckedInt<int32_t>(aStride_u) + aStride_v))
           .isValid()) {
    return false;
  }
  return true;
}

const GMPVideoi420FrameImpl::Plane* GMPVideoi420FrameImpl::GetPlane(
    GMPPlaneType aType) const {
  switch (aType) {
    case kGMPYPlane:
      return &mYPlane;
    case kGMPUPlane:
      return &mUPlane;
    case kGMPVPlane:
      return &mVPlane;
    default:
      MOZ_CRASH("Unknown plane type!");
  }
  return nullptr;
}

GMPVideoi420FrameImpl::Plane* GMPVideoi420FrameImpl::GetPlane(
    GMPPlaneType aType) {
  switch (aType) {
    case kGMPYPlane:
      return &mYPlane;
    case kGMPUPlane:
      return &mUPlane;
    case kGMPVPlane:
      return &mVPlane;
    default:
      MOZ_CRASH("Unknown plane type!");
  }
  return nullptr;
}

GMPErr GMPVideoi420FrameImpl::CreateEmptyFrame(int32_t aWidth, int32_t aHeight,
                                               int32_t aStride_y,
                                               int32_t aStride_u,
                                               int32_t aStride_v) {
  if (!CheckDimensions(aWidth, aHeight, aStride_y, aStride_u, aStride_v)) {
    return GMPGenericErr;
  }

  int32_t size_y = aStride_y * aHeight;
  int32_t half_height = (aHeight + 1) / 2;
  int32_t size_u = aStride_u * half_height;
  int32_t size_v = aStride_v * half_height;

  // Ensure we align our buffers optimally for SIMD operations.
  constexpr int32_t offset_y = 0;
  int32_t offset_u = gfx::GetAlignedStride<32>(offset_y + size_y, 1);
  int32_t offset_v = gfx::GetAlignedStride<32>(offset_u + size_u, 1);
  int32_t buffer_size = offset_v + size_v;

  if (offset_u <= 0 || offset_v <= 0 || buffer_size <= 0) {
    return GMPGenericErr;
  }

  // Allocate the unified buffer.
  GMPErr err = MaybeResize(buffer_size);
  if (err != GMPNoErr) {
    return err;
  }

  mYPlane = {offset_y, size_y, aStride_y};
  mUPlane = {offset_u, size_u, aStride_u};
  mVPlane = {offset_v, size_v, aStride_v};
  mWidth = aWidth;
  mHeight = aHeight;
  mTimestamp = 0ll;
  mDuration = 0ll;

  return GMPNoErr;
}

GMPErr GMPVideoi420FrameImpl::CreateFrame(
    int32_t aSize_y, const uint8_t* aBuffer_y, int32_t aSize_u,
    const uint8_t* aBuffer_u, int32_t aSize_v, const uint8_t* aBuffer_v,
    int32_t aWidth, int32_t aHeight, int32_t aStride_y, int32_t aStride_u,
    int32_t aStride_v) {
  MOZ_ASSERT(aBuffer_y);
  MOZ_ASSERT(aBuffer_u);
  MOZ_ASSERT(aBuffer_v);

  if (aSize_y < 1 || aSize_u < 1 || aSize_v < 1) {
    return GMPGenericErr;
  }

  if (!CheckDimensions(aWidth, aHeight, aStride_y, aStride_u, aStride_v)) {
    return GMPGenericErr;
  }

  // Ensure we align our buffers optimally for SIMD operations.
  constexpr int32_t offset_y = 0;
  int32_t offset_u = gfx::GetAlignedStride<32>(offset_y + aSize_y, 1);
  int32_t offset_v = gfx::GetAlignedStride<32>(offset_u + aSize_u, 1);
  int32_t buffer_size = offset_v + aSize_v;

  if (offset_u <= 0 || offset_v <= 0 || buffer_size <= 0) {
    return GMPGenericErr;
  }

  // Allocate the unified buffer.
  GMPErr err = MaybeResize(buffer_size);
  if (err != GMPNoErr) {
    return err;
  }

  auto* buffer = BufferPtr();
  memcpy(buffer + offset_y, aBuffer_y, aSize_y);
  memcpy(buffer + offset_u, aBuffer_u, aSize_u);
  memcpy(buffer + offset_v, aBuffer_v, aSize_v);

  mYPlane = {offset_y, aSize_y, aStride_y};
  mUPlane = {offset_u, aSize_u, aStride_u};
  mVPlane = {offset_v, aSize_v, aStride_v};
  mWidth = aWidth;
  mHeight = aHeight;
  return GMPNoErr;
}

GMPErr GMPVideoi420FrameImpl::CopyFrame(const GMPVideoi420Frame& aFrame) {
  auto& f = static_cast<const GMPVideoi420FrameImpl&>(aFrame);

  int32_t size = f.AllocatedSize();
  if (size <= 0) {
    return GMPGenericErr;
  }

  GMPErr err = MaybeResize(size);
  if (err != GMPNoErr) {
    return err;
  }

  const auto* src = f.BufferPtr();
  auto* dst = BufferPtr();
  if (!dst || !src) {
    return GMPGenericErr;
  }

  mYPlane = f.mYPlane;
  mUPlane = f.mUPlane;
  mVPlane = f.mVPlane;
  mWidth = f.mWidth;
  mHeight = f.mHeight;
  mTimestamp = f.mTimestamp;
  mDuration = f.mDuration;

  memcpy(dst + mYPlane.mOffset, src + f.mYPlane.mOffset, mYPlane.mSize);
  memcpy(dst + mUPlane.mOffset, src + f.mUPlane.mOffset, mUPlane.mSize);
  memcpy(dst + mVPlane.mOffset, src + f.mVPlane.mOffset, mVPlane.mSize);
  return GMPNoErr;
}

void GMPVideoi420FrameImpl::SwapFrame(GMPVideoi420Frame* aFrame) {
  auto f = static_cast<GMPVideoi420FrameImpl*>(aFrame);
  std::swap(mHost, f->mHost);
  std::swap(mBuffer, f->mBuffer);
  std::swap(mYPlane, f->mYPlane);
  std::swap(mUPlane, f->mUPlane);
  std::swap(mVPlane, f->mVPlane);
  std::swap(mWidth, f->mWidth);
  std::swap(mHeight, f->mHeight);
  std::swap(mTimestamp, f->mTimestamp);
  std::swap(mDuration, f->mDuration);
}

uint8_t* GMPVideoi420FrameImpl::Buffer(GMPPlaneType aType) {
  if (mBuffer.IsWritable()) {
    auto* p = GetPlane(aType);
    if (p) {
      return BufferPtr() + p->mOffset;
    }
  }
  return nullptr;
}

const uint8_t* GMPVideoi420FrameImpl::Buffer(GMPPlaneType aType) const {
  if (mBuffer.IsReadable()) {
    const auto* p = GetPlane(aType);
    if (p) {
      return BufferPtr() + p->mOffset;
    }
  }
  return nullptr;
}

int32_t GMPVideoi420FrameImpl::AllocatedSize(GMPPlaneType aType) const {
  if (mBuffer.IsWritable()) {
    const auto* p = GetPlane(aType);
    if (p) {
      return p->mSize;
    }
  }
  return -1;
}

int32_t GMPVideoi420FrameImpl::Stride(GMPPlaneType aType) const {
  const auto* p = GetPlane(aType);
  if (p) {
    return p->mStride;
  }
  return -1;
}

GMPErr GMPVideoi420FrameImpl::SetWidth(int32_t aWidth) {
  if (!CheckDimensions(aWidth, mHeight, mYPlane.mStride, mUPlane.mStride,
                       mVPlane.mStride)) {
    return GMPGenericErr;
  }
  mWidth = aWidth;
  return GMPNoErr;
}

GMPErr GMPVideoi420FrameImpl::SetHeight(int32_t aHeight) {
  if (!CheckDimensions(mWidth, aHeight, mYPlane.mStride, mUPlane.mStride,
                       mVPlane.mStride)) {
    return GMPGenericErr;
  }
  mHeight = aHeight;
  return GMPNoErr;
}

int32_t GMPVideoi420FrameImpl::Width() const { return mWidth; }

int32_t GMPVideoi420FrameImpl::Height() const { return mHeight; }

void GMPVideoi420FrameImpl::SetTimestamp(uint64_t aTimestamp) {
  mTimestamp = aTimestamp;
}

uint64_t GMPVideoi420FrameImpl::Timestamp() const { return mTimestamp; }

void GMPVideoi420FrameImpl::SetDuration(uint64_t aDuration) {
  mDuration = aDuration;
}

uint64_t GMPVideoi420FrameImpl::Duration() const { return mDuration; }

bool GMPVideoi420FrameImpl::IsZeroSize() const {
  return mYPlane.mSize == 0 && mUPlane.mSize == 0 && mVPlane.mSize == 0;
}

void GMPVideoi420FrameImpl::ResetSize() {
  mYPlane.mSize = 0;
  mUPlane.mSize = 0;
  mVPlane.mSize = 0;
}

}  // namespace mozilla::gmp
