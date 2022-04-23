/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/dom/UnionTypes.h"
#include "Queue.h"

#include "CommandBuffer.h"
#include "CommandEncoder.h"
#include "ipc/WebGPUChild.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/OffscreenCanvas.h"
#include "mozilla/dom/WebGLTexelConversions.h"
#include "mozilla/dom/WebGLTypes.h"

namespace mozilla {
namespace webgpu {

GPU_IMPL_CYCLE_COLLECTION(Queue, mParent, mBridge)
GPU_IMPL_JS_WRAP(Queue)

Queue::Queue(Device* const aParent, WebGPUChild* aBridge, RawId aId)
    : ChildOf(aParent), mBridge(aBridge), mId(aId) {}

Queue::~Queue() { Cleanup(); }

void Queue::Submit(
    const dom::Sequence<OwningNonNull<CommandBuffer>>& aCommandBuffers) {
  nsTArray<RawId> list(aCommandBuffers.Length());
  for (uint32_t i = 0; i < aCommandBuffers.Length(); ++i) {
    auto idMaybe = aCommandBuffers[i]->Commit();
    if (idMaybe) {
      list.AppendElement(*idMaybe);
    }
  }

  mBridge->SendQueueSubmit(mId, mParent->mId, list);
}

void Queue::WriteBuffer(const Buffer& aBuffer, uint64_t aBufferOffset,
                        const dom::ArrayBufferViewOrArrayBuffer& aData,
                        uint64_t aDataOffset,
                        const dom::Optional<uint64_t>& aSize,
                        ErrorResult& aRv) {
  uint64_t length = 0;
  uint8_t* data = nullptr;
  if (aData.IsArrayBufferView()) {
    const auto& view = aData.GetAsArrayBufferView();
    view.ComputeState();
    length = view.Length();
    data = view.Data();
  }
  if (aData.IsArrayBuffer()) {
    const auto& ab = aData.GetAsArrayBuffer();
    ab.ComputeState();
    length = ab.Length();
    data = ab.Data();
  }
  MOZ_ASSERT(data != nullptr);

  const auto checkedSize = aSize.WasPassed()
                               ? CheckedInt<size_t>(aSize.Value())
                               : CheckedInt<size_t>(length) - aDataOffset;
  if (!checkedSize.isValid()) {
    aRv.ThrowRangeError("Mapped size is too large");
    return;
  }

  const auto& size = checkedSize.value();
  if (aDataOffset + size > length) {
    aRv.ThrowAbortError(nsPrintfCString("Wrong data size %" PRIuPTR, size));
    return;
  }

  ipc::Shmem shmem;
  if (!mBridge->AllocShmem(size, ipc::Shmem::SharedMemory::TYPE_BASIC,
                           &shmem)) {
    aRv.ThrowAbortError(
        nsPrintfCString("Unable to allocate shmem of size %" PRIuPTR, size));
    return;
  }

  memcpy(shmem.get<uint8_t>(), data + aDataOffset, size);
  ipc::ByteBuf bb;
  ffi::wgpu_queue_write_buffer(aBuffer.mId, aBufferOffset, ToFFI(&bb));
  if (!mBridge->SendQueueWriteAction(mId, mParent->mId, std::move(bb),
                                     std::move(shmem))) {
    MOZ_CRASH("IPC failure");
  }
}

void Queue::WriteTexture(const dom::GPUImageCopyTexture& aDestination,
                         const dom::ArrayBufferViewOrArrayBuffer& aData,
                         const dom::GPUImageDataLayout& aDataLayout,
                         const dom::GPUExtent3D& aSize, ErrorResult& aRv) {
  ffi::WGPUImageCopyTexture copyView = {};
  CommandEncoder::ConvertTextureCopyViewToFFI(aDestination, &copyView);
  ffi::WGPUImageDataLayout dataLayout = {};
  CommandEncoder::ConvertTextureDataLayoutToFFI(aDataLayout, &dataLayout);
  dataLayout.offset = 0;  // our Shmem has the contents starting from 0.
  ffi::WGPUExtent3d extent = {};
  CommandEncoder::ConvertExtent3DToFFI(aSize, &extent);

  uint64_t availableSize = 0;
  uint8_t* data = nullptr;
  if (aData.IsArrayBufferView()) {
    const auto& view = aData.GetAsArrayBufferView();
    view.ComputeState();
    availableSize = view.Length();
    data = view.Data();
  }
  if (aData.IsArrayBuffer()) {
    const auto& ab = aData.GetAsArrayBuffer();
    ab.ComputeState();
    availableSize = ab.Length();
    data = ab.Data();
  }
  MOZ_ASSERT(data != nullptr);

  const auto checkedSize =
      CheckedInt<size_t>(availableSize) - aDataLayout.mOffset;
  if (!checkedSize.isValid()) {
    aRv.ThrowAbortError(nsPrintfCString("Offset is higher than the size"));
    return;
  }
  const auto size = checkedSize.value();

  ipc::Shmem shmem;
  if (!mBridge->AllocShmem(size, ipc::Shmem::SharedMemory::TYPE_BASIC,
                           &shmem)) {
    aRv.ThrowAbortError(
        nsPrintfCString("Unable to allocate shmem of size %" PRIuPTR, size));
    return;
  }

  memcpy(shmem.get<uint8_t>(), data + aDataLayout.mOffset, size);
  ipc::ByteBuf bb;
  ffi::wgpu_queue_write_texture(copyView, dataLayout, extent, ToFFI(&bb));
  if (!mBridge->SendQueueWriteAction(mId, mParent->mId, std::move(bb),
                                     std::move(shmem))) {
    MOZ_CRASH("IPC failure");
  }
}

static WebGLTexelFormat ToWebGLTexelFormat(gfx::SurfaceFormat aFormat) {
  switch (aFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
      return WebGLTexelFormat::BGRA8;
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::R8G8B8X8:
      return WebGLTexelFormat::RGBA8;
    default:
      return WebGLTexelFormat::FormatNotSupportingAnyConversion;
  }
}

static WebGLTexelFormat ToWebGLTexelFormat(dom::GPUTextureFormat aFormat) {
  /* These are the formats we need to support:
    "r8unorm"
    "r16float"
    "r32float"
    "rg8unorm"
    "rg16float"
    "rg32float"
    "rgba8unorm"
    "rgba8unorm-srgb"
    "bgra8unorm"
    "bgra8unorm-srgb"
    "rgb10a2unorm"
    "rgba16float"
    "rgba32float"
   */
  // FIXME(aosmond): We need support for Rbg10a2unorm as well.
  switch (aFormat) {
    case dom::GPUTextureFormat::R8unorm:
      return WebGLTexelFormat::R8;
    case dom::GPUTextureFormat::R16float:
      return WebGLTexelFormat::R16F;
    case dom::GPUTextureFormat::R32float:
      return WebGLTexelFormat::R32F;
    case dom::GPUTextureFormat::Rg8unorm:
      return WebGLTexelFormat::RG8;
    case dom::GPUTextureFormat::Rg16float:
      return WebGLTexelFormat::RG16F;
    case dom::GPUTextureFormat::Rg32float:
      return WebGLTexelFormat::RG32F;
    case dom::GPUTextureFormat::Rgba8unorm:
    case dom::GPUTextureFormat::Rgba8unorm_srgb:
      return WebGLTexelFormat::RGBA8;
    case dom::GPUTextureFormat::Bgra8unorm:
    case dom::GPUTextureFormat::Bgra8unorm_srgb:
      return WebGLTexelFormat::BGRA8;
    case dom::GPUTextureFormat::Rgba16float:
      return WebGLTexelFormat::RGBA16F;
    case dom::GPUTextureFormat::Rgba32float:
      return WebGLTexelFormat::RGBA32F;
    default:
      return WebGLTexelFormat::FormatNotSupportingAnyConversion;
  }
}

void Queue::CopyExternalImageToTexture(
    const dom::GPUImageCopyExternalImage& aSource,
    const dom::GPUImageCopyTextureTagged& aDestination,
    const dom::GPUExtent3D& aCopySize, ErrorResult& aRv) {
  const auto dstFormat = ToWebGLTexelFormat(aDestination.mTexture->mFormat);
  if (dstFormat == WebGLTexelFormat::FormatNotSupportingAnyConversion) {
    aRv.ThrowInvalidStateError("Unsupported destination format");
    return;
  }

  if (!aDestination.mTexture->mBytesPerBlock) {
    aRv.ThrowInvalidStateError("Unknown destination block size");
    return;
  }

  RefPtr<gfx::SourceSurface> surface;
  if (aSource.mSource.IsImageBitmap()) {
    const auto& bitmap = aSource.mSource.GetAsImageBitmap();
  } else if (aSource.mSource.IsHTMLCanvasElement()) {
    const auto& canvas = aSource.mSource.GetAsHTMLCanvasElement();
    surface = canvas->GetSurfaceSnapshot(nullptr);
  } else if (aSource.mSource.IsOffscreenCanvas()) {
    const auto& canvas = aSource.mSource.GetAsOffscreenCanvas();
    surface = canvas->GetSurfaceSnapshot(nullptr);
  }

  if (!surface) {
    aRv.ThrowInvalidStateError("No surface available from source");
    return;
  }

  RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    aRv.ThrowInvalidStateError("No data surface available from source");
    return;
  }

  const auto surfaceFormat = dataSurface->GetFormat();
  const auto srcFormat = ToWebGLTexelFormat(surfaceFormat);
  if (srcFormat == WebGLTexelFormat::FormatNotSupportingAnyConversion) {
    aRv.ThrowInvalidStateError("Unsupported surface format from source");
    return;
  }

  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::READ);
  if (!map.IsMapped()) {
    aRv.ThrowInvalidStateError("Cannot map surface from source");
    return;
  }

  if (!aSource.mOrigin.IsGPUOrigin2DDict()) {
    aRv.ThrowInvalidStateError("Cannot get origin from source");
    return;
  }

  uint32_t srcOriginX = aSource.mOrigin.GetAsGPUOrigin2DDict().mX;
  uint32_t srcOriginY = aSource.mOrigin.GetAsGPUOrigin2DDict().mY;

  const gfx::IntSize surfaceSize = dataSurface->GetSize();
  if (static_cast<uint32_t>(surfaceSize.width) < srcOriginX ||
      static_cast<uint32_t>(surfaceSize.height) < srcOriginY) {
    aRv.ThrowInvalidStateError("Offset is outside surface from source");
    return;
  }

  uint32_t width = surfaceSize.width - srcOriginX;
  uint32_t height = surfaceSize.height - srcOriginY;

  const auto dstStride = CheckedInt<uint32_t>(width) *
                         aDestination.mTexture->mBytesPerBlock.value();
  const auto dstLength = dstStride * height;
  if (!dstStride.isValid() || !dstLength.isValid()) {
    aRv.ThrowInvalidStateError("Destination buffer dimensions invalid");
    return;
  }

  ipc::Shmem shmem;
  if (!mBridge->AllocShmem(dstLength.value(),
                           ipc::Shmem::SharedMemory::TYPE_BASIC, &shmem)) {
    aRv.ThrowAbortError(nsPrintfCString("Unable to allocate shmem of size %u",
                                        dstLength.value()));
    return;
  }

  int32_t pixelSize = gfx::BytesPerPixel(surfaceFormat);
  const auto* srcBegin =
      map.GetData() + srcOriginX * pixelSize + srcOriginY * map.GetStride();
  const auto dstOriginPos =
      aSource.mFlipY ? gl::OriginPos::BottomLeft : gl::OriginPos::TopLeft;
  bool wasTrivial;
  if (!ConvertImage(width, height, srcBegin, map.GetStride(),
                    gl::OriginPos::TopLeft, srcFormat,
                    /* srcPremultiplied */ true, shmem.get<uint8_t*>(),
                    dstStride.value(), dstOriginPos, dstFormat,
                    aDestination.mPremultipliedAlpha, &wasTrivial)) {
    MOZ_ASSERT_UNREACHABLE("ConvertImage failed!");
    aRv.ThrowInvalidStateError(
        "Failed to convert source to destination format");
    mBridge->DeallocShmem(shmem);
    return;
  }

  ffi::WGPUImageDataLayout dataLayout = {0, dstStride.value(), height};
  ffi::WGPUExtent3d extent = {width, height, 0};
  ffi::WGPUImageCopyTexture copyView = {};
  CommandEncoder::ConvertTextureCopyViewToFFI(aDestination, &copyView);
  ipc::ByteBuf bb;
  ffi::wgpu_queue_write_texture(copyView, dataLayout, extent, ToFFI(&bb));
  if (!mBridge->SendQueueWriteAction(mId, mParent->mId, std::move(bb),
                                     std::move(shmem))) {
    MOZ_CRASH("IPC failure");
  }
}

/*
bool ConvertImage(size_t width, size_t height, const void* srcBegin,
                  size_t srcStride, gl::OriginPos srcOrigin,
                  WebGLTexelFormat srcFormat, bool srcPremultiplied,
                  void* dstBegin, size_t dstStride, gl::OriginPos dstOrigin,
                  WebGLTexelFormat dstFormat, bool dstPremultiplied,
                  bool* const out_wasTrivial)
*/

}  // namespace webgpu
}  // namespace mozilla
