/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Encoder.h"
#include "mozilla/CheckedInt.h"

namespace mozilla {

using namespace gfx;

namespace image {

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
  return InitFromData(aData, IntSize(aWidth, aHeight), aStride,
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

  return StartImageEncode(IntSize(aWidth, aHeight), format,
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
  return AddImageFrame(aData, IntSize(aWidth, aHeight), aStride,
                       ToSurfaceFormat(aInputFormat),
                       ToSurfaceFlags(aInputFormat), aFrameOptions);
}

}  // namespace image
}  // namespace mozilla
