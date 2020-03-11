/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_Encoder_h
#define mozilla_image_Encoder_h

#include "imgIEncoder.h"
#include "mozilla/gfx/Types.h"  // for SurfaceFormat
#include "mozilla/gfx/2D.h"     // for DataSurfaceFlags
#include "mozilla/gfx/Point.h"  // for IntSize

namespace mozilla {
namespace image {

class ImageEncoder : public imgIEncoder {
 public:
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

 private:
  nsresult VerifyParameters(uint32_t aLength, uint32_t aWidth, uint32_t aHeight,
                            uint32_t aStride, uint32_t aInputFormat) const;
  gfx::SurfaceFormat ToSurfaceFormat(uint32_t aInputFormat) const;
  gfx::DataSurfaceFlags ToSurfaceFlags(uint32_t aInputFormat) const;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_Encoder_h
