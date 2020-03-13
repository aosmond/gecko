/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCRT.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/UniquePtrExtensions.h"
#include "nsBMPEncoder.h"
#include "nsString.h"
#include "nsTArray.h"
#include "mozilla/CheckedInt.h"
#include "BMPHeaders.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::image::bmp;

nsBMPEncoder::nsBMPEncoder() : mBMPInfoHeader{} {
  this->mBMPFileHeader.filesize = 0;
  this->mBMPFileHeader.reserved = 0;
  this->mBMPFileHeader.dataoffset = 0;
}

nsBMPEncoder::~nsBMPEncoder() {}

// Just a helper method to make it explicit in calculations that we are dealing
// with bytes and not bits
static inline uint16_t BytesPerPixel(uint16_t aBPP) { return aBPP / 8; }

// Calculates the number of padding bytes that are needed per row of image data
static inline uint32_t PaddingBytes(uint16_t aBPP, uint32_t aWidth) {
  uint32_t rowSize = aWidth * BytesPerPixel(aBPP);
  uint8_t paddingSize = 0;
  if (rowSize % 4) {
    paddingSize = (4 - (rowSize % 4));
  }
  return paddingSize;
}

// See ::InitFromData for other info.
nsresult nsBMPEncoder::StartSurfaceDataEncode(const IntSize& aSize,
                                              SurfaceFormat aFormat,
                                              DataSurfaceFlags aFlags,
                                              const nsAString& aOptions) {
  // can't initialize more than once
  if (HasBuffer()) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // parse and check any provided output options
  Version version;
  uint16_t bpp;
  nsresult rv = ParseOptions(aOptions, aFormat, version, bpp);
  if (NS_FAILED(rv)) {
    return rv;
  }
  MOZ_ASSERT(bpp <= 32);

  rv = InitFileHeader(version, bpp, aSize.width, aSize.height);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = InitInfoHeader(version, bpp, aSize.width, aSize.height);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = AllocateBuffer(mBMPFileHeader.filesize);
  if (NS_FAILED(rv)) {
    return rv;
  }

  EncodeFileHeader();
  EncodeInfoHeader();

  return NS_OK;
}

nsresult nsBMPEncoder::AddSurfaceDataFrame(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  // must be initialized
  if (!HasBuffer()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  SurfaceFormat outFormat;
  if (mBMPInfoHeader.bpp == 24) {
    outFormat = SurfaceFormat::B8G8R8;
  } else if (mBMPInfoHeader.bihsize == InfoHeaderLength::WIN_V5) {
    outFormat = SurfaceFormat::B8G8R8A8;
  } else {
    outFormat = SurfaceFormat::B8G8R8X8;
  }

  nsresult rv = PrepareRowPipeline(aFormat, aFlags, outFormat, aSize);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (mBMPInfoHeader.width < 0) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  CheckedUint32 size = CheckedUint32(mBMPInfoHeader.width) *
                       CheckedUint32(BytesPerPixel(mBMPInfoHeader.bpp));
  if (MOZ_UNLIKELY(!size.isValid())) {
    return NS_ERROR_FAILURE;
  }

  CheckedUint32 check = CheckedUint32(mBMPInfoHeader.height) * aStride;
  if (MOZ_UNLIKELY(!check.isValid())) {
    return NS_ERROR_FAILURE;
  }

  if (RemainingBytesToWrite() < size.value() * aSize.height) {
    MOZ_ASSERT_UNREACHABLE("Buffer overrun?");
    return NS_ERROR_FAILURE;
  }

  uint32_t padding = PaddingBytes(mBMPInfoHeader.bpp, aSize.width);
  for (int32_t y = mBMPInfoHeader.height - 1; y >= 0; --y) {
    const uint8_t* row = PrepareRow(aData, aStride, aSize.width, y);
    rv = WriteBuffer(row, size.value());
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = WriteBufferByte(0, padding);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  return NS_OK;
}

// Parses the encoder options and sets the bits per pixel to use
// See InitFromData for a description of the parse options
nsresult nsBMPEncoder::ParseOptions(const nsAString& aOptions,
                                    SurfaceFormat aFormat, Version& aVersionOut,
                                    uint16_t& aBppOut) {
  // Default to version 5 so that we can capture alpha channels.
  aVersionOut = VERSION_5;

  // Default should be to preserve as much information as possible.
  aBppOut = IsOpaque(aFormat) ? 24 : 32;

  // Parse the input string into a set of name/value pairs.
  // From a format like: name=value;bpp=<bpp_value>;name=value
  // to format: [0] = name=value, [1] = bpp=<bpp_value>, [2] = name=value
  nsTArray<nsCString> nameValuePairs;
  if (!ParseString(NS_ConvertUTF16toUTF8(aOptions), ';', nameValuePairs)) {
    return NS_ERROR_INVALID_ARG;
  }

  // For each name/value pair in the set
  for (uint32_t i = 0; i < nameValuePairs.Length(); ++i) {
    // Split the name value pair [0] = name, [1] = value
    nsTArray<nsCString> nameValuePair;
    if (!ParseString(nameValuePairs[i], '=', nameValuePair)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (nameValuePair.Length() != 2) {
      return NS_ERROR_INVALID_ARG;
    }

    // Parse the bpp portion of the string name=value;version=<version_value>;
    // name=value
    if (nameValuePair[0].Equals("version",
                                nsCaseInsensitiveCStringComparator())) {
      if (nameValuePair[1].EqualsLiteral("3")) {
        aVersionOut = VERSION_3;
      } else if (nameValuePair[1].EqualsLiteral("5")) {
        aVersionOut = VERSION_5;
      } else {
        return NS_ERROR_INVALID_ARG;
      }
    }

    // Parse the bpp portion of the string name=value;bpp=<bpp_value>;name=value
    if (nameValuePair[0].Equals("bpp", nsCaseInsensitiveCStringComparator())) {
      if (nameValuePair[1].EqualsLiteral("24")) {
        aBppOut = 24;
      } else if (nameValuePair[1].EqualsLiteral("32")) {
        aBppOut = 32;
      } else {
        return NS_ERROR_INVALID_ARG;
      }
    }
  }

  return NS_OK;
}

// Initializes the BMP file header mBMPFileHeader to the passed in values
nsresult nsBMPEncoder::InitFileHeader(Version aVersion, uint16_t aBPP,
                                      uint32_t aWidth, uint32_t aHeight) {
  memset(&mBMPFileHeader, 0, sizeof(mBMPFileHeader));
  mBMPFileHeader.signature[0] = 'B';
  mBMPFileHeader.signature[1] = 'M';

  if (aVersion == VERSION_3) {
    mBMPFileHeader.dataoffset = FILE_HEADER_LENGTH + InfoHeaderLength::WIN_V3;
  } else {  // aVersion == 5
    mBMPFileHeader.dataoffset = FILE_HEADER_LENGTH + InfoHeaderLength::WIN_V5;
  }

  // The color table is present only if BPP is <= 8
  if (aBPP <= 8) {
    uint32_t numColors = 1 << aBPP;
    mBMPFileHeader.dataoffset += 4 * numColors;
    CheckedUint32 filesize = CheckedUint32(mBMPFileHeader.dataoffset) +
                             CheckedUint32(aWidth) * aHeight;
    if (MOZ_UNLIKELY(!filesize.isValid())) {
      return NS_ERROR_INVALID_ARG;
    }
    mBMPFileHeader.filesize = filesize.value();
  } else {
    CheckedUint32 filesize = CheckedUint32(mBMPFileHeader.dataoffset) +
                             (CheckedUint32(aWidth) * BytesPerPixel(aBPP) +
                              PaddingBytes(aBPP, aWidth)) *
                                 aHeight;
    if (MOZ_UNLIKELY(!filesize.isValid())) {
      return NS_ERROR_INVALID_ARG;
    }
    mBMPFileHeader.filesize = filesize.value();
  }

  mBMPFileHeader.reserved = 0;

  return NS_OK;
}

// Initializes the bitmap info header mBMPInfoHeader to the passed in values
nsresult nsBMPEncoder::InitInfoHeader(Version aVersion, uint16_t aBPP,
                                      uint32_t aWidth, uint32_t aHeight) {
  memset(&mBMPInfoHeader, 0, sizeof(mBMPInfoHeader));
  if (aVersion == VERSION_3) {
    mBMPInfoHeader.bihsize = InfoHeaderLength::WIN_V3;
  } else {
    MOZ_ASSERT(aVersion == VERSION_5);
    mBMPInfoHeader.bihsize = InfoHeaderLength::WIN_V5;
  }

  CheckedInt32 width(aWidth);
  CheckedInt32 height(aHeight);
  if (MOZ_UNLIKELY(!width.isValid() || !height.isValid())) {
    return NS_ERROR_INVALID_ARG;
  }
  mBMPInfoHeader.width = width.value();
  mBMPInfoHeader.height = height.value();

  mBMPInfoHeader.planes = 1;
  mBMPInfoHeader.bpp = aBPP;
  if (aBPP == 24 || aVersion == VERSION_3) {
    mBMPInfoHeader.compression = 0;
  } else {
    mBMPInfoHeader.compression = 3; // Using bitfields to capture alpha
  }
  mBMPInfoHeader.colors = 0;
  mBMPInfoHeader.important_colors = 0;

  CheckedUint32 check = CheckedUint32(aWidth) * BytesPerPixel(aBPP);
  if (MOZ_UNLIKELY(!check.isValid())) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aBPP <= 8) {
    CheckedUint32 imagesize = CheckedUint32(aWidth) * aHeight;
    if (MOZ_UNLIKELY(!imagesize.isValid())) {
      return NS_ERROR_INVALID_ARG;
    }
    mBMPInfoHeader.image_size = imagesize.value();
  } else {
    CheckedUint32 imagesize = (CheckedUint32(aWidth) * BytesPerPixel(aBPP) +
                               PaddingBytes(aBPP, aWidth)) *
                              CheckedUint32(aHeight);
    if (MOZ_UNLIKELY(!imagesize.isValid())) {
      return NS_ERROR_INVALID_ARG;
    }
    mBMPInfoHeader.image_size = imagesize.value();
  }
  mBMPInfoHeader.xppm = 0;
  mBMPInfoHeader.yppm = 0;
  if (aVersion >= VERSION_5) {
    mBMPInfoHeader.red_mask = 0x00FF0000;
    mBMPInfoHeader.green_mask = 0x0000FF00;
    mBMPInfoHeader.blue_mask = 0x000000FF;
    mBMPInfoHeader.alpha_mask = 0xFF000000;
    mBMPInfoHeader.color_space = V5InfoHeader::COLOR_SPACE_LCS_SRGB;
    mBMPInfoHeader.white_point.r.x = 0;
    mBMPInfoHeader.white_point.r.y = 0;
    mBMPInfoHeader.white_point.r.z = 0;
    mBMPInfoHeader.white_point.g.x = 0;
    mBMPInfoHeader.white_point.g.y = 0;
    mBMPInfoHeader.white_point.g.z = 0;
    mBMPInfoHeader.white_point.b.x = 0;
    mBMPInfoHeader.white_point.b.y = 0;
    mBMPInfoHeader.white_point.b.z = 0;
    mBMPInfoHeader.gamma_red = 0;
    mBMPInfoHeader.gamma_green = 0;
    mBMPInfoHeader.gamma_blue = 0;
    mBMPInfoHeader.intent = 0;
    mBMPInfoHeader.profile_offset = 0;
    mBMPInfoHeader.profile_size = 0;
    mBMPInfoHeader.reserved = 0;
  }

  return NS_OK;
}

// Encodes the BMP file header mBMPFileHeader
void nsBMPEncoder::EncodeFileHeader() {
  FileHeader littleEndianBFH = mBMPFileHeader;
  NativeEndian::swapToLittleEndianInPlace(&littleEndianBFH.filesize, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianBFH.reserved, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianBFH.dataoffset, 1);

  WriteBuffer(littleEndianBFH.signature);
  WriteBuffer(littleEndianBFH.filesize);
  WriteBuffer(littleEndianBFH.reserved);
  WriteBuffer(littleEndianBFH.dataoffset);
}

// Encodes the BMP info header mBMPInfoHeader
void nsBMPEncoder::EncodeInfoHeader() {
  V5InfoHeader littleEndianmBIH = mBMPInfoHeader;
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.bihsize, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.width, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.height, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.planes, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.bpp, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.compression, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.image_size, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.xppm, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.yppm, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.colors, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.important_colors,
                                          1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.red_mask, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.green_mask, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.blue_mask, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.alpha_mask, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.color_space, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.r.x, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.r.y, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.r.z, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.g.x, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.g.y, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.g.z, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.b.x, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.b.y, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.white_point.b.z, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.gamma_red, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.gamma_green, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.gamma_blue, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.intent, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.profile_offset, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmBIH.profile_size, 1);

  WriteBuffer(littleEndianmBIH.bihsize);
  WriteBuffer(littleEndianmBIH.width);
  WriteBuffer(littleEndianmBIH.height);
  WriteBuffer(littleEndianmBIH.planes);
  WriteBuffer(littleEndianmBIH.bpp);
  WriteBuffer(littleEndianmBIH.compression);
  WriteBuffer(littleEndianmBIH.image_size);
  WriteBuffer(littleEndianmBIH.xppm);
  WriteBuffer(littleEndianmBIH.yppm);
  WriteBuffer(littleEndianmBIH.colors);
  WriteBuffer(littleEndianmBIH.important_colors);

  if (mBMPInfoHeader.bihsize > InfoHeaderLength::WIN_V3) {
    WriteBuffer(littleEndianmBIH.red_mask);
    WriteBuffer(littleEndianmBIH.green_mask);
    WriteBuffer(littleEndianmBIH.blue_mask);
    WriteBuffer(littleEndianmBIH.alpha_mask);
    WriteBuffer(littleEndianmBIH.color_space);
    WriteBuffer(littleEndianmBIH.white_point.r.x);
    WriteBuffer(littleEndianmBIH.white_point.r.y);
    WriteBuffer(littleEndianmBIH.white_point.r.z);
    WriteBuffer(littleEndianmBIH.white_point.g.x);
    WriteBuffer(littleEndianmBIH.white_point.g.y);
    WriteBuffer(littleEndianmBIH.white_point.g.z);
    WriteBuffer(littleEndianmBIH.white_point.b.x);
    WriteBuffer(littleEndianmBIH.white_point.b.y);
    WriteBuffer(littleEndianmBIH.white_point.b.z);
    WriteBuffer(littleEndianmBIH.gamma_red);
    WriteBuffer(littleEndianmBIH.gamma_green);
    WriteBuffer(littleEndianmBIH.gamma_blue);
    WriteBuffer(littleEndianmBIH.intent);
    WriteBuffer(littleEndianmBIH.profile_offset);
    WriteBuffer(littleEndianmBIH.profile_size);
    WriteBuffer(littleEndianmBIH.reserved);
  }
}
