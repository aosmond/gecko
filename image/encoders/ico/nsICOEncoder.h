/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_encoders_ico_nsICOEncoder_h
#define mozilla_image_encoders_ico_nsICOEncoder_h

#include "mozilla/Attributes.h"
#include "mozilla/image/ICOFileHeaders.h"
#include "mozilla/image/Encoder.h"

// Provides ICO encoding functionality. Use InitFromData() to do the
// encoding. See that function definition for encoding options.

class nsICOEncoder final : public mozilla::image::ImageEncoder {
 public:
  nsICOEncoder();

  // Obtains the width of the icon directory entry
  uint32_t GetRealWidth() const {
    return mICODirEntry.mWidth == 0 ? 256 : mICODirEntry.mWidth;
  }

  // Obtains the height of the icon directory entry
  uint32_t GetRealHeight() const {
    return mICODirEntry.mHeight == 0 ? 256 : mICODirEntry.mHeight;
  }

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
  ~nsICOEncoder() override;

  nsresult ParseOptions(const nsAString& aOptions,
                        mozilla::gfx::SurfaceFormat aFormat, uint16_t& aBppOut,
                        bool& aUsePNGOut);

  // Initializes the icon file header mICOFileHeader
  void InitFileHeader();
  // Initializes the icon directory info header mICODirEntry
  void InitInfoHeader(uint16_t aBPP, uint8_t aWidth, uint8_t aHeight);
  // Encodes the icon file header mICOFileHeader
  void EncodeFileHeader();
  // Encodes the icon directory info header mICODirEntry
  void EncodeInfoHeader();

  // Holds either a PNG or a BMP depending on the encoding options specified
  // or if no encoding options specified will use the default (PNG)
  RefPtr<ImageEncoder> mContainedEncoder;

  // These headers will always contain endian independent stuff.
  // Don't trust the width and height of mICODirEntry directly,
  // instead use the accessors GetRealWidth() and GetRealHeight().
  mozilla::image::IconFileHeader mICOFileHeader;
  mozilla::image::IconDirEntry mICODirEntry;

  // Stores true if the contained image is a PNG
  bool mUsePNG;
};

#endif  // mozilla_image_encoders_ico_nsICOEncoder_h
