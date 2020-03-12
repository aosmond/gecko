/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_encoders_bmp_nsBMPEncoder_h
#define mozilla_image_encoders_bmp_nsBMPEncoder_h

#include "mozilla/Attributes.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/UniquePtr.h"

#include "mozilla/image/Encoder.h"

#include "nsCOMPtr.h"

namespace mozilla {
namespace image {
namespace bmp {

struct FileHeader {
  char signature[2];    // String "BM".
  uint32_t filesize;    // File size.
  int32_t reserved;     // Zero.
  uint32_t dataoffset;  // Offset to raster data.
};

struct XYZ {
  int32_t x, y, z;
};

struct XYZTriple {
  XYZ r, g, b;
};

struct V5InfoHeader {
  uint32_t bihsize;           // Header size
  int32_t width;              // Uint16 in OS/2 BMPs
  int32_t height;             // Uint16 in OS/2 BMPs
  uint16_t planes;            // =1
  uint16_t bpp;               // Bits per pixel.
  uint32_t compression;       // See Compression for valid values
  uint32_t image_size;        // (compressed) image size. Can be 0 if
                              // compression==0
  uint32_t xppm;              // Pixels per meter, horizontal
  uint32_t yppm;              // Pixels per meter, vertical
  uint32_t colors;            // Used Colors
  uint32_t important_colors;  // Number of important colors. 0=all
  // The rest of the header is not available in WIN_V3 BMP Files
  uint32_t red_mask;     // Bits used for red component
  uint32_t green_mask;   // Bits used for green component
  uint32_t blue_mask;    // Bits used for blue component
  uint32_t alpha_mask;   // Bits used for alpha component
  uint32_t color_space;  // 0x73524742=LCS_sRGB ...
  // These members are unused unless color_space == LCS_CALIBRATED_RGB
  XYZTriple white_point;  // Logical white point
  uint32_t gamma_red;     // Red gamma component
  uint32_t gamma_green;   // Green gamma component
  uint32_t gamma_blue;    // Blue gamma component
  uint32_t intent;        // Rendering intent
  // These members are unused unless color_space == LCS_PROFILE_*
  uint32_t profile_offset;  // Offset to profile data in bytes
  uint32_t profile_size;    // Size of profile data in bytes
  uint32_t reserved;        // =0

  static const uint32_t COLOR_SPACE_LCS_SRGB = 0x73524742;
};

}  // namespace bmp
}  // namespace image
}  // namespace mozilla

// Provides BMP encoding functionality. Use InitFromData() to do the
// encoding. See that function definition for encoding options.

class nsBMPEncoder final : public mozilla::image::ImageEncoder {
  typedef mozilla::ReentrantMonitor ReentrantMonitor;

 public:
  nsBMPEncoder();

  // From ImageEncoder
  nsresult StartSurfaceDataEncode(const mozilla::gfx::IntSize& aSize,
                                  mozilla::gfx::SurfaceFormat aFormat,
                                  mozilla::gfx::DataSurfaceFlags aFlags,
                                  const nsAString& outputOptions) override;

  nsresult AddSurfaceDataFrame(const uint8_t* aData,
                               const mozilla::gfx::IntSize& aSize,
                               int32_t aStride,
                               mozilla::gfx::SurfaceFormat aFormat,
                               mozilla::gfx::DataSurfaceFlags aFlags,
                               const nsAString& aOptions) override;

 protected:
  ~nsBMPEncoder() override;

  enum Version { VERSION_3 = 3, VERSION_5 = 5 };

  // See InitData in the cpp for valid parse options
  nsresult ParseOptions(const nsAString& aOptions,
                        mozilla::gfx::SurfaceFormat aFormat,
                        Version& aVersionOut, uint16_t& aBppOut);

  // Initializes the bitmap file header member mBMPFileHeader
  nsresult InitFileHeader(Version aVersion, uint16_t aBPP, uint32_t aWidth,
                          uint32_t aHeight);
  // Initializes the bitmap info header member mBMPInfoHeader
  nsresult InitInfoHeader(Version aVersion, uint16_t aBPP, uint32_t aWidth,
                          uint32_t aHeight);

  // Encodes the bitmap file header member mBMPFileHeader
  void EncodeFileHeader();
  // Encodes the bitmap info header member mBMPInfoHeader
  void EncodeInfoHeader();

  // These headers will always contain endian independent stuff
  // They store the BMP headers which will be encoded
  mozilla::image::bmp::FileHeader mBMPFileHeader;
  mozilla::image::bmp::V5InfoHeader mBMPInfoHeader;
};

#endif  // mozilla_image_encoders_bmp_nsBMPEncoder_h
