/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Encoder.h"
#include "nsStreamUtils.h"
#include "mozilla/CheckedInt.h"

namespace mozilla {

using namespace gfx;

namespace image {

NS_IMPL_ISUPPORTS(ImageEncoder, imgIEncoder, nsIInputStream,
                  nsIAsyncInputStream)

ImageEncoder::ImageEncoder()
    : mImageBuffer(nullptr),
      mImageBufferSize(0),
      mImageBufferWritePoint(0),
      mImageBufferReadPoint(0),
      mFinished(false),
      mCallback(nullptr),
      mCallbackTarget(nullptr),
      mNotifyThreshold(0),
      mSwizzleFn(nullptr),
      mPremultiplyFn(nullptr) {}

ImageEncoder::~ImageEncoder() {
  if (mImageBuffer) {
    free(mImageBuffer);
  }
}

nsresult ImageEncoder::VerifyParameters(uint32_t aLength, uint32_t aWidth,
                                        uint32_t aHeight, uint32_t aStride,
                                        uint32_t aInputFormat) const {
  if (aLength > INT32_MAX || aWidth > INT32_MAX || aHeight > INT32_MAX ||
      aStride > INT32_MAX) {
    return NS_ERROR_INVALID_ARG;
  }

  SurfaceFormat format = ToSurfaceFormat(aInputFormat);
  if (format == SurfaceFormat::UNKNOWN) {
    return NS_ERROR_INVALID_ARG;
  }

  CheckedInt32 dataLen =
      CheckedInt32(aStride) * CheckedInt32(aHeight) * BytesPerPixel(format);
  if (!dataLen.isValid() || dataLen.value() > int32_t(aLength)) {
    return NS_ERROR_INVALID_ARG;
  }

  CheckedInt32 minStride = CheckedInt32(aWidth) * BytesPerPixel(format);
  if (!minStride.isValid() || minStride.value() > int32_t(aStride)) {
    return NS_ERROR_INVALID_ARG;
  }

  return NS_OK;
}

SurfaceFormat ImageEncoder::ToSurfaceFormat(uint32_t aInputFormat) const {
  switch (aInputFormat) {
    case INPUT_FORMAT_RGB:
      return SurfaceFormat::R8G8B8;
    case INPUT_FORMAT_RGBA:
      return SurfaceFormat::R8G8B8A8;
    case INPUT_FORMAT_HOSTARGB:
      return SurfaceFormat::A8R8G8B8_UINT32;
    default:
      return SurfaceFormat::UNKNOWN;
  }
}

DataSurfaceFlags ImageEncoder::ToSurfaceFlags(uint32_t aInputFormat) const {
  switch (aInputFormat) {
    case INPUT_FORMAT_RGB:
    case INPUT_FORMAT_RGBA:
      // The encoders assume that RGBA implies unpremultiplied.
      return DataSurfaceFlags::UNPREMULTIPLIED_ALPHA;
    case INPUT_FORMAT_HOSTARGB:
    default:
      return DataSurfaceFlags::NONE;
  }
}

nsresult ImageEncoder::PrepareRowPipeline(SurfaceFormat aIn,
                                          DataSurfaceFlags aInFlags,
                                          SurfaceFormat aOut,
                                          const IntSize& aSize) {
  mSwizzleFn = SwizzleRow(aIn, aOut);
  if (!mSwizzleFn) {
    return NS_ERROR_FAILURE;
  }

  if (aInFlags & DataSurfaceFlags::UNPREMULTIPLIED_ALPHA) {
    // Surface is unpremultiplied. If we are dropping the alpha, we actually
    // need to perform premultiplication first. If we are keeping the alpha, we
    // don't need to do anything.
    if (IsOpaque(aOut)) {
      mPremultiplyFn = PremultiplyRow(aIn, aIn);
      if (!mPremultiplyFn) {
        return NS_ERROR_FAILURE;
      }
    }
  } else {
    // Surface is premultiplied. If we are dropping the alpha, we don't need to
    // do anything. If we are keeping the alpha, we need to reverse the
    // premultiplication first.
    if (!IsOpaque(aOut)) {
      mPremultiplyFn = UnpremultiplyRow(aIn, aIn);
      if (!mPremultiplyFn) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  mRowBuffer.reset(new (fallible) uint8_t[aSize.width * 4]);
  if (!mRowBuffer) {
    return NS_ERROR_FAILURE;
  }
}

const uint8_t* ImageEncoder::PrepareRow(const uint8_t* aData, int32_t aStride,
                                        int32_t aWidth, int32_t aRow) {
  MOZ_ASSERT(mSwizzleFn);
  MOZ_ASSERT(mRowBuffer);

  const uint8_t* src = aData + aStride * aRow;

  if (mPremultiplyFn) {
    mPremultiplyFn(src, mRowBuffer.get(), aWidth);
    mSwizzleFn(mRowBuffer.get(), mRowBuffer.get(), aWidth);
  } else {
    mSwizzleFn(src, mRowBuffer.get(), aWidth);
  }

  return mRowBuffer.get();
}

nsresult ImageEncoder::InitFromSurfaceData(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  NS_ENSURE_ARG(aData);

  nsresult rv;
  rv = StartSurfaceDataEncode(aSize, aFormat, aFlags, aOptions);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = AddSurfaceDataFrame(aData, aSize, aStride, aFormat, aFlags, aOptions);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = EndImageEncode();
  return rv;
}

NS_IMETHODIMP
ImageEncoder::InitFromData(const uint8_t* aData,
                           uint32_t aLength,  // (unused, req'd by JS)
                           uint32_t aWidth, uint32_t aHeight, uint32_t aStride,
                           uint32_t aInputFormat,
                           const nsAString& aOutputOptions) {
  nsresult rv =
      VerifyParameters(aLength, aWidth, aHeight, aStride, aInputFormat);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return InitFromSurfaceData(aData, IntSize(aWidth, aHeight), aStride,
                             ToSurfaceFormat(aInputFormat),
                             ToSurfaceFlags(aInputFormat), aOutputOptions);
}

NS_IMETHODIMP ImageEncoder::StartImageEncode(uint32_t aWidth, uint32_t aHeight,
                                             uint32_t aInputFormat,
                                             const nsAString& aOptions) {
  if (aWidth > INT32_MAX || aHeight > INT32_MAX) {
    return NS_ERROR_INVALID_ARG;
  }

  SurfaceFormat format = ToSurfaceFormat(aInputFormat);
  if (format == SurfaceFormat::UNKNOWN) {
    return NS_ERROR_INVALID_ARG;
  }

  CheckedInt32 dataLen =
      CheckedInt32(aWidth) * CheckedInt32(aHeight) * BytesPerPixel(format);
  if (!dataLen.isValid()) {
    return NS_ERROR_INVALID_ARG;
  }

  return StartSurfaceDataEncode(IntSize(aWidth, aHeight), format,
                                ToSurfaceFlags(aInputFormat), aOptions);
}

NS_IMETHODIMP
ImageEncoder::AddImageFrame(const uint8_t* aData,
                            uint32_t aLength,  // (unused, req'd by JS)
                            uint32_t aWidth, uint32_t aHeight, uint32_t aStride,
                            uint32_t aInputFormat,
                            const nsAString& aFrameOptions) {
  nsresult rv =
      VerifyParameters(aLength, aWidth, aHeight, aStride, aInputFormat);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return AddSurfaceDataFrame(aData, IntSize(aWidth, aHeight), aStride,
                             ToSurfaceFormat(aInputFormat),
                             ToSurfaceFlags(aInputFormat), aFrameOptions);
}

NS_IMETHODIMP
ImageEncoder::GetImageBufferUsed(uint32_t* aOutputSize) {
  NS_ENSURE_ARG_POINTER(aOutputSize);
  *aOutputSize = mImageBufferSize;
  return NS_OK;
}

// Returns a pointer to the start of the image buffer
NS_IMETHODIMP
ImageEncoder::GetImageBuffer(char** aOutputBuffer) {
  NS_ENSURE_ARG_POINTER(aOutputBuffer);
  *aOutputBuffer = reinterpret_cast<char*>(mImageBuffer);
  return NS_OK;
}

NS_IMETHODIMP
ImageEncoder::EndImageEncode() {
  // must be initialized
  if (!mImageBuffer || !mImageBufferWritePoint) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mFinished = true;
  NotifyListener();

  // if output callback can't get enough memory, it will free our buffer
  if (!mImageBuffer || !mImageBufferWritePoint) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

nsresult ImageEncoder::AllocateBuffer(uint32_t aSize) {
  if (mImageBuffer) {
    MOZ_ASSERT_UNREACHABLE("Buffer already allocated");
    return NS_ERROR_FAILURE;
  }

  mImageBuffer = static_cast<uint8_t*>(malloc(aSize));
  if (!mImageBuffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mImageBufferSize = aSize;
  return NS_OK;
}

nsresult ImageEncoder::ReallocateBuffer(uint32_t aSize) {
  if (mImageBufferWritePoint > aSize) {
    MOZ_ASSERT_UNREACHABLE("Buffer reallocate to too small");
    return NS_ERROR_FAILURE;
  }

  uint8_t* buf = static_cast<uint8_t*>(realloc(mImageBuffer, aSize));
  if (!buf) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mImageBuffer = buf;
  mImageBufferSize = aSize;
  return NS_OK;
}

nsresult ImageEncoder::AdvanceBuffer(uint32_t aAdvance) {
  if (mImageBufferWritePoint + aAdvance > mImageBufferSize) {
    MOZ_ASSERT_UNREACHABLE("Buffer overrun?");
    return NS_ERROR_FAILURE;
  }
  mImageBufferWritePoint += aAdvance;
  return NS_OK;
}

void ImageEncoder::ReleaseBuffer() {
  free(mImageBuffer);
  mImageBuffer = nullptr;
  mImageBufferSize = 0;
  mImageBufferWritePoint = 0;
  mImageBufferReadPoint = 0;
}

NS_IMETHODIMP
ImageEncoder::Close() {
  ReleaseBuffer();
  return NS_OK;
}

// Obtains the available bytes to read
NS_IMETHODIMP
ImageEncoder::Available(uint64_t* _retval) {
  if (!mImageBuffer) {
    return NS_BASE_STREAM_CLOSED;
  }

  *_retval = RemainingBytesToRead();
  return NS_OK;
}

// [noscript] Reads bytes which are available
NS_IMETHODIMP
ImageEncoder::Read(char* aBuf, uint32_t aCount, uint32_t* _retval) {
  return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, _retval);
}

// [noscript] Reads segments
NS_IMETHODIMP
ImageEncoder::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                           uint32_t aCount, uint32_t* _retval) {
  uint32_t maxCount = RemainingBytesToRead();
  if (maxCount == 0) {
    *_retval = 0;
    return mFinished ? NS_OK : NS_BASE_STREAM_WOULD_BLOCK;
  }

  if (aCount > maxCount) {
    aCount = maxCount;
  }
  nsresult rv = aWriter(
      this, aClosure,
      reinterpret_cast<const char*>(mImageBuffer + mImageBufferReadPoint), 0,
      aCount, _retval);
  if (NS_SUCCEEDED(rv)) {
    NS_ASSERTION(*_retval <= aCount, "bad write count");
    mImageBufferReadPoint += *_retval;
  }
  // errors returned from the writer end here!
  return NS_OK;
}

NS_IMETHODIMP
ImageEncoder::IsNonBlocking(bool* _retval) {
  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
ImageEncoder::AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
                        uint32_t aRequestedCount, nsIEventTarget* aTarget) {
  if (aFlags != 0) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (mCallback || mCallbackTarget) {
    return NS_ERROR_UNEXPECTED;
  }

  mCallbackTarget = aTarget;
  // 0 means "any number of bytes except 0"
  mNotifyThreshold = aRequestedCount;
  if (!aRequestedCount) {
    mNotifyThreshold = 1024;  // We don't want to notify incessantly
  }

  // We set the callback absolutely last, because NotifyListener uses it to
  // determine if someone needs to be notified.  If we don't set it last,
  // NotifyListener might try to fire off a notification to a null target
  // which will generally cause non-threadsafe objects to be used off the
  // main thread
  mCallback = aCallback;

  // What we are being asked for may be present already
  NotifyListener();
  return NS_OK;
}

NS_IMETHODIMP
ImageEncoder::CloseWithStatus(nsresult aStatus) { return Close(); }

void ImageEncoder::NotifyListener() {
  if (mCallback && (RemainingBytesToRead() >= mNotifyThreshold || mFinished)) {
    nsCOMPtr<nsIInputStreamCallback> callback;
    if (mCallbackTarget) {
      callback = NS_NewInputStreamReadyEvent("nsBMPEncoder::NotifyListener",
                                             mCallback, mCallbackTarget);
    } else {
      callback = mCallback;
    }

    NS_ASSERTION(callback, "Shouldn't fail to make the callback");
    // Null the callback first because OnInputStreamReady could
    // reenter AsyncWait
    mCallback = nullptr;
    mCallbackTarget = nullptr;
    mNotifyThreshold = 0;

    callback->OnInputStreamReady(this);
  }
}

}  // namespace image
}  // namespace mozilla
