/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_Encoder_h
#define mozilla_image_Encoder_h

#include "imgIEncoder.h"
#include "mozilla/gfx/Types.h"    // for SurfaceFormat
#include "mozilla/gfx/2D.h"       // for DataSurfaceFlags
#include "mozilla/gfx/Point.h"    // for IntSize
#include "mozilla/gfx/Swizzle.h"  // for SwizzleRowFn

namespace mozilla {
namespace image {

class ImageEncoder : public imgIEncoder {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINPUTSTREAM
  NS_DECL_NSIASYNCINPUTSTREAM

  ImageEncoder();

  // From imgIEncoder, used by JavaScript.
  NS_IMETHOD InitFromData(const uint8_t* data, uint32_t length, uint32_t width,
                          uint32_t height, uint32_t stride,
                          uint32_t inputFormat,
                          const nsAString& outputOptions) final;
  NS_IMETHOD StartImageEncode(uint32_t width, uint32_t height,
                              uint32_t inputFormat,
                              const nsAString& outputOptions) final;
  NS_IMETHOD AddImageFrame(const uint8_t* data, uint32_t length, uint32_t width,
                           uint32_t height, uint32_t stride,
                           uint32_t frameFormat,
                           const nsAString& frameOptions) final;

  // From imgIEncoder
  NS_IMETHOD GetImageBufferUsed(uint32_t* aOutputSize) final;
  NS_IMETHOD GetImageBuffer(char** aOutputBuffer) final;
  NS_IMETHOD EndImageEncode(void) override;

  nsresult InitFromSurfaceData(const uint8_t* aData, const gfx::IntSize& aSize,
                               int32_t aStride, gfx::SurfaceFormat aFormat,
                               gfx::DataSurfaceFlags aFlags,
                               const nsAString& aOptions) override;

 protected:
  virtual ~ImageEncoder();

  nsresult PrepareRowPipeline(gfx::SurfaceFormat aIn,
                              gfx::DataSurfaceFlags aInFlags,
                              gfx::SurfaceFormat aOut,
                              const gfx::IntSize& aSize);
  const uint8_t* PrepareRow(const uint8_t* aData, int32_t aStride,
                            int32_t aWidth, int32_t aRow);

  nsresult AllocateBuffer(uint32_t aSize);
  nsresult ReallocateBuffer(uint32_t aSize);
  nsresult AdvanceBuffer(uint32_t aSize);
  void ReleaseBuffer();

  uint8_t* BufferHead() const { return mImageBuffer + mImageBufferWritePoint; }

  template <typename T>
  nsresult WriteBuffer(const T& aData) {
    if (sizeof(aData) > RemainingBytesToWrite()) {
      MOZ_ASSERT_UNREACHABLE("Buffer overrun?");
      return NS_ERROR_FAILURE;
    }
    memcpy(BufferHead(), &aData, sizeof(aData));
    mImageBufferWritePoint += sizeof(aData);
    return NS_OK;
  }

  nsresult WriteBuffer(const void* aData, size_t aDataSize) {
    if (aDataSize > RemainingBytesToWrite()) {
      MOZ_ASSERT_UNREACHABLE("Buffer overrun?");
      return NS_ERROR_FAILURE;
    }
    memcpy(BufferHead(), aData, aDataSize);
    mImageBufferWritePoint += aDataSize;
    return NS_OK;
  }

  nsresult WriteBufferByte(uint8_t aData, size_t aDataSize) {
    if (aDataSize > RemainingBytesToWrite()) {
      MOZ_ASSERT_UNREACHABLE("Buffer overrun?");
      return NS_ERROR_FAILURE;
    }
    memset(BufferHead(), aData, aDataSize);
    mImageBufferWritePoint += aDataSize;
    return NS_OK;
  }

  bool HasBuffer() const { return !!mImageBuffer; }

  uint32_t BufferSize() const { return mImageBufferSize; }

  uint32_t BufferSizeWritten() const { return mImageBufferWritePoint; }

  uint32_t RemainingBytesToWrite() const {
    MOZ_ASSERT(mImageBufferSize >= mImageBufferWritePoint);
    return mImageBufferSize - mImageBufferWritePoint;
  }

  virtual void NotifyListener();

 private:
  nsresult VerifyParameters(uint32_t aLength, uint32_t aWidth, uint32_t aHeight,
                            uint32_t aStride, uint32_t aInputFormat) const;
  gfx::SurfaceFormat ToSurfaceFormat(uint32_t aInputFormat) const;
  gfx::DataSurfaceFlags ToSurfaceFlags(uint32_t aInputFormat) const;

  uint32_t RemainingBytesToRead() const {
    MOZ_ASSERT(mImageBufferWritePoint <= mImageBufferReadPoint);
    return mImageBufferWritePoint - mImageBufferReadPoint;
  }

  // Keeps track of the start of the image buffer
  uint8_t* mImageBuffer;
  // Keeps track of the image buffer size
  uint32_t mImageBufferSize;
  // Keeps track of the number of bytes in the image buffer which are written
  uint32_t mImageBufferWritePoint;
  // Keeps track of the number of bytes in the image buffer which are read
  uint32_t mImageBufferReadPoint;
  // Stores true if the image is done being encoded
  bool mFinished;

  nsCOMPtr<nsIInputStreamCallback> mCallback;
  nsCOMPtr<nsIEventTarget> mCallbackTarget;
  uint32_t mNotifyThreshold;

  UniquePtr<uint8_t[]> mRowBuffer;
  gfx::SwizzleRowFn mSwizzleFn;
  gfx::SwizzleRowFn mPremultiplyFn;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_Encoder_h
