/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCRT.h"
#include "mozilla/EndianUtils.h"
#include "nsBMPEncoder.h"
#include "BMPHeaders.h"
#include "nsPNGEncoder.h"
#include "nsICOEncoder.h"
#include "nsString.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::image;
using namespace mozilla::gfx;

nsICOEncoder::nsICOEncoder()
    : mICOFileHeader{}, mICODirEntry{}, mUsePNG(true) {}

nsICOEncoder::~nsICOEncoder() {}

nsresult nsICOEncoder::AddSurfaceDataFrame(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  if (mUsePNG) {
    mContainedEncoder = new nsPNGEncoder();
    nsresult rv;
    nsAutoString noParams;
    rv = mContainedEncoder->InitFromSurfaceData(aData, aSize, aStride, aFormat,
                                                aFlags, noParams);
    NS_ENSURE_SUCCESS(rv, rv);

    uint32_t PNGImageBufferSize;
    mContainedEncoder->GetImageBufferUsed(&PNGImageBufferSize);
    rv = AllocateBuffer(ICONFILEHEADERSIZE + ICODIRENTRYSIZE +
                        PNGImageBufferSize);
    NS_ENSURE_SUCCESS(rv, rv);
    mICODirEntry.mBytesInRes = PNGImageBufferSize;

    EncodeFileHeader();
    EncodeInfoHeader();

    char* imageBuffer;
    rv = mContainedEncoder->GetImageBuffer(&imageBuffer);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteBuffer(imageBuffer, PNGImageBufferSize);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    mContainedEncoder = new nsBMPEncoder();
    nsresult rv;

    // If we are storing a BMP, it should be version 3.
    nsAutoString params;
    params.AppendLiteral("version=3;bpp=");
    params.AppendInt(mICODirEntry.mBitCount);

    rv = mContainedEncoder->InitFromSurfaceData(aData, aSize, aStride, aFormat,
                                                aFlags, params);
    NS_ENSURE_SUCCESS(rv, rv);

    uint32_t andMaskSize = ((GetRealWidth() + 31) / 32) * 4 *  // row AND mask
                           GetRealHeight();                    // num rows

    uint32_t BMPImageBufferSize;
    mContainedEncoder->GetImageBufferUsed(&BMPImageBufferSize);
    rv = AllocateBuffer(ICONFILEHEADERSIZE + ICODIRENTRYSIZE +
                        BMPImageBufferSize + andMaskSize);
    NS_ENSURE_SUCCESS(rv, rv);

    // Icon files that wrap a BMP file must not include the BITMAPFILEHEADER
    // section at the beginning of the encoded BMP data, so we must skip over
    // bmp::FILE_HEADER_LENGTH bytes when adding the BMP content to the icon
    // file.
    mICODirEntry.mBytesInRes =
        BMPImageBufferSize - bmp::FILE_HEADER_LENGTH + andMaskSize;

    // Encode the icon headers
    EncodeFileHeader();
    EncodeInfoHeader();
    uint8_t* imageHeader = BufferHead();

    char* imageBuffer;
    rv = mContainedEncoder->GetImageBuffer(&imageBuffer);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteBuffer(imageBuffer + bmp::FILE_HEADER_LENGTH,
                     BMPImageBufferSize - bmp::FILE_HEADER_LENGTH);
    NS_ENSURE_SUCCESS(rv, rv);
    // We need to fix the BMP height to be *2 for the AND mask
    uint32_t fixedHeight = GetRealHeight() * 2;
    NativeEndian::swapToLittleEndianInPlace(&fixedHeight, 1);
    // The height is stored at an offset of 8 from the DIB header
    memcpy(imageHeader + 8, &fixedHeight, sizeof(fixedHeight));

    // Write out the AND mask
    rv = WriteBufferByte(0, andMaskSize);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// Two output options are supported: format=<png|bmp>;bpp=<bpp_value>
// format specifies whether to use png or bitmap format
// bpp specifies the bits per pixel to use where bpp_value can be 24 or 32
nsresult nsICOEncoder::StartSurfaceDataEncode(const IntSize& aSize,
                                              SurfaceFormat aFormat,
                                              DataSurfaceFlags aFlags,
                                              const nsAString& aOptions) {
  // can't initialize more than once
  if (HasBuffer()) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // Icons are only 1 byte, so make sure our bitmap is in range
  if (aSize.width > 256 || aSize.height > 256) {
    return NS_ERROR_INVALID_ARG;
  }

  // parse and check any provided output options
  uint16_t bpp;
  nsresult rv = ParseOptions(aOptions, aFormat, bpp, mUsePNG);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(bpp <= 32);

  InitFileHeader();
  // The width and height are stored as 0 when we have a value of 256
  InitInfoHeader(bpp, aSize.width == 256 ? 0 : (uint8_t)aSize.width,
                 aSize.height == 256 ? 0 : (uint8_t)aSize.height);

  return NS_OK;
}

// Parses the encoder options and sets the bits per pixel to use and PNG or BMP
// See InitFromData for a description of the parse options
nsresult nsICOEncoder::ParseOptions(const nsAString& aOptions,
                                    SurfaceFormat aFormat, uint16_t& aBppOut,
                                    bool& aUsePNGOut) {
  // If no parsing options just use the default of 24BPP and PNG yes
  // Default should be to preserve as much information as possible.
  aBppOut = IsOpaque(aFormat) ? 24 : 32;
  aUsePNGOut = true;

  // Parse the input string into a set of name/value pairs.
  // From format: format=<png|bmp>;bpp=<bpp_value>
  // to format: [0] = format=<png|bmp>, [1] = bpp=<bpp_value>
  nsTArray<nsCString> nameValuePairs;
  if (!ParseString(NS_ConvertUTF16toUTF8(aOptions), ';', nameValuePairs)) {
    return NS_ERROR_INVALID_ARG;
  }

  // For each name/value pair in the set
  for (unsigned i = 0; i < nameValuePairs.Length(); ++i) {
    // Split the name value pair [0] = name, [1] = value
    nsTArray<nsCString> nameValuePair;
    if (!ParseString(nameValuePairs[i], '=', nameValuePair)) {
      return NS_ERROR_INVALID_ARG;
    }
    if (nameValuePair.Length() != 2) {
      return NS_ERROR_INVALID_ARG;
    }

    // Parse the format portion of the string format=<png|bmp>;bpp=<bpp_value>
    if (nameValuePair[0].Equals("format",
                                nsCaseInsensitiveCStringComparator())) {
      if (nameValuePair[1].Equals("png",
                                  nsCaseInsensitiveCStringComparator())) {
        aUsePNGOut = true;
      } else if (nameValuePair[1].Equals(
                     "bmp", nsCaseInsensitiveCStringComparator())) {
        aUsePNGOut = false;
      } else {
        return NS_ERROR_INVALID_ARG;
      }
    }

    // Parse the bpp portion of the string format=<png|bmp>;bpp=<bpp_value>
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

// Initializes the icon file header mICOFileHeader
void nsICOEncoder::InitFileHeader() {
  memset(&mICOFileHeader, 0, sizeof(mICOFileHeader));
  mICOFileHeader.mReserved = 0;
  mICOFileHeader.mType = 1;
  mICOFileHeader.mCount = 1;
}

// Initializes the icon directory info header mICODirEntry
void nsICOEncoder::InitInfoHeader(uint16_t aBPP, uint8_t aWidth,
                                  uint8_t aHeight) {
  memset(&mICODirEntry, 0, sizeof(mICODirEntry));
  mICODirEntry.mBitCount = aBPP;
  mICODirEntry.mBytesInRes = 0;
  mICODirEntry.mColorCount = 0;
  mICODirEntry.mWidth = aWidth;
  mICODirEntry.mHeight = aHeight;
  mICODirEntry.mImageOffset = ICONFILEHEADERSIZE + ICODIRENTRYSIZE;
  mICODirEntry.mPlanes = 1;
  mICODirEntry.mReserved = 0;
}

// Encodes the icon file header mICOFileHeader
void nsICOEncoder::EncodeFileHeader() {
  IconFileHeader littleEndianIFH = mICOFileHeader;
  NativeEndian::swapToLittleEndianInPlace(&littleEndianIFH.mReserved, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianIFH.mType, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianIFH.mCount, 1);

  WriteBuffer(littleEndianIFH.mReserved);
  WriteBuffer(littleEndianIFH.mType);
  WriteBuffer(littleEndianIFH.mCount);
}

// Encodes the icon directory info header mICODirEntry
void nsICOEncoder::EncodeInfoHeader() {
  IconDirEntry littleEndianmIDE = mICODirEntry;

  NativeEndian::swapToLittleEndianInPlace(&littleEndianmIDE.mPlanes, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmIDE.mBitCount, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmIDE.mBytesInRes, 1);
  NativeEndian::swapToLittleEndianInPlace(&littleEndianmIDE.mImageOffset, 1);

  WriteBuffer(littleEndianmIDE.mWidth);
  WriteBuffer(littleEndianmIDE.mHeight);
  WriteBuffer(littleEndianmIDE.mColorCount);
  WriteBuffer(littleEndianmIDE.mReserved);
  WriteBuffer(littleEndianmIDE.mPlanes);
  WriteBuffer(littleEndianmIDE.mBitCount);
  WriteBuffer(littleEndianmIDE.mBytesInRes);
  WriteBuffer(littleEndianmIDE.mImageOffset);
}
