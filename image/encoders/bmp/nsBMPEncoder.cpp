/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCRT.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/Swizzle.h"
#include "nsBMPEncoder.h"
#include "nsString.h"
#include "nsStreamUtils.h"
#include "nsTArray.h"
#include "mozilla/CheckedInt.h"
#include "BMPHeaders.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::image::bmp;

NS_IMPL_ISUPPORTS(nsBMPEncoder, imgIEncoder, nsIInputStream,
                  nsIAsyncInputStream)

nsBMPEncoder::nsBMPEncoder()
    : mBMPInfoHeader{},
      mImageBufferStart(nullptr),
      mImageBufferCurr(0),
      mImageBufferSize(0),
      mImageBufferReadPoint(0),
      mFinished(false),
      mCallback(nullptr),
      mCallbackTarget(nullptr),
      mNotifyThreshold(0) {
  this->mBMPFileHeader.filesize = 0;
  this->mBMPFileHeader.reserved = 0;
  this->mBMPFileHeader.dataoffset = 0;
}

nsBMPEncoder::~nsBMPEncoder() {
  if (mImageBufferStart) {
    free(mImageBufferStart);
    mImageBufferStart = nullptr;
    mImageBufferCurr = nullptr;
  }
}

// nsBMPEncoder::InitFromData
//
// One output option is supported: bpp=<bpp_value>
// bpp specifies the bits per pixel to use where bpp_value can be 24 or 32
nsresult nsBMPEncoder::InitFromSurfaceData(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
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
  if (mImageBufferStart || mImageBufferCurr) {
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

  mImageBufferSize = mBMPFileHeader.filesize;
  mImageBufferStart = static_cast<uint8_t*>(malloc(mImageBufferSize));
  if (!mImageBufferStart) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mImageBufferCurr = mImageBufferStart;

  EncodeFileHeader();
  EncodeInfoHeader();

  return NS_OK;
}

// Returns the number of bytes in the image buffer used.
// For a BMP file, this is all bytes in the buffer.
NS_IMETHODIMP
nsBMPEncoder::GetImageBufferUsed(uint32_t* aOutputSize) {
  NS_ENSURE_ARG_POINTER(aOutputSize);
  *aOutputSize = mImageBufferSize;
  return NS_OK;
}

// Returns a pointer to the start of the image buffer
NS_IMETHODIMP
nsBMPEncoder::GetImageBuffer(char** aOutputBuffer) {
  NS_ENSURE_ARG_POINTER(aOutputBuffer);
  *aOutputBuffer = reinterpret_cast<char*>(mImageBufferStart);
  return NS_OK;
}

nsresult nsBMPEncoder::AddSurfaceDataFrame(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  // must be initialized
  if (!mImageBufferStart || !mImageBufferCurr) {
    return NS_ERROR_NOT_INITIALIZED;
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

  SurfaceFormat outFormat = mBMPInfoHeader.bpp == 24 ? SurfaceFormat::R8G8B8
                                                     : SurfaceFormat::R8G8B8A8;

  if (outFormat == SurfaceFormat::R8G8B8 && !IsOpaque(aFormat) &&
      aFlags & DataSurfaceFlags::UNPREMULTIPLIED_ALPHA) {
    // FIXME(aosmond): we need to premultiply before we store, but we don't have
    // enough room in the buffer
    return NS_ERROR_FAILURE;
  }

  if (!SwizzleData(aData, aStride, aFormat, mImageBufferCurr, size.value(),
                   outFormat, aSize)) {
    return NS_ERROR_FAILURE;
  }

  if (!IsOpaque(aFormat) &&
      !(aFlags & DataSurfaceFlags::UNPREMULTIPLIED_ALPHA)) {
    if (!UnpremultiplyData(mImageBufferCurr, size.value(), outFormat,
                           mImageBufferCurr, size.value(), outFormat, aSize)) {
      return NS_ERROR_FAILURE;
    }
  }

  mImageBufferCurr += size.value() * mBMPInfoHeader.height;

  return NS_OK;
}

NS_IMETHODIMP
nsBMPEncoder::EndImageEncode() {
  // must be initialized
  if (!mImageBufferStart || !mImageBufferCurr) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  mFinished = true;
  NotifyListener();

  // if output callback can't get enough memory, it will free our buffer
  if (!mImageBufferStart || !mImageBufferCurr) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

// Parses the encoder options and sets the bits per pixel to use
// See InitFromData for a description of the parse options
nsresult nsBMPEncoder::ParseOptions(const nsAString& aOptions,
                                    SurfaceFormat aFormat, Version& aVersionOut,
                                    uint16_t& aBppOut) {
  aVersionOut = VERSION_3;

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

NS_IMETHODIMP
nsBMPEncoder::Close() {
  if (mImageBufferStart) {
    free(mImageBufferStart);
    mImageBufferStart = nullptr;
    mImageBufferSize = 0;
    mImageBufferReadPoint = 0;
    mImageBufferCurr = nullptr;
  }

  return NS_OK;
}

// Obtains the available bytes to read
NS_IMETHODIMP
nsBMPEncoder::Available(uint64_t* _retval) {
  if (!mImageBufferStart || !mImageBufferCurr) {
    return NS_BASE_STREAM_CLOSED;
  }

  *_retval = GetCurrentImageBufferOffset() - mImageBufferReadPoint;
  return NS_OK;
}

// [noscript] Reads bytes which are available
NS_IMETHODIMP
nsBMPEncoder::Read(char* aBuf, uint32_t aCount, uint32_t* _retval) {
  return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, _retval);
}

// [noscript] Reads segments
NS_IMETHODIMP
nsBMPEncoder::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                           uint32_t aCount, uint32_t* _retval) {
  uint32_t maxCount = GetCurrentImageBufferOffset() - mImageBufferReadPoint;
  if (maxCount == 0) {
    *_retval = 0;
    return mFinished ? NS_OK : NS_BASE_STREAM_WOULD_BLOCK;
  }

  if (aCount > maxCount) {
    aCount = maxCount;
  }
  nsresult rv = aWriter(
      this, aClosure,
      reinterpret_cast<const char*>(mImageBufferStart + mImageBufferReadPoint),
      0, aCount, _retval);
  if (NS_SUCCEEDED(rv)) {
    NS_ASSERTION(*_retval <= aCount, "bad write count");
    mImageBufferReadPoint += *_retval;
  }
  // errors returned from the writer end here!
  return NS_OK;
}

NS_IMETHODIMP
nsBMPEncoder::IsNonBlocking(bool* _retval) {
  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
nsBMPEncoder::AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
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
nsBMPEncoder::CloseWithStatus(nsresult aStatus) { return Close(); }

void nsBMPEncoder::NotifyListener() {
  if (mCallback && (GetCurrentImageBufferOffset() - mImageBufferReadPoint >=
                        mNotifyThreshold ||
                    mFinished)) {
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

#define ENCODE(pImageBufferCurr, value)            \
  memcpy(*pImageBufferCurr, &value, sizeof value); \
  *pImageBufferCurr += sizeof value;

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
  mBMPInfoHeader.compression = 0;
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
    mBMPInfoHeader.red_mask = 0x000000FF;
    mBMPInfoHeader.green_mask = 0x0000FF00;
    mBMPInfoHeader.blue_mask = 0x00FF0000;
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

  ENCODE(&mImageBufferCurr, littleEndianBFH.signature);
  ENCODE(&mImageBufferCurr, littleEndianBFH.filesize);
  ENCODE(&mImageBufferCurr, littleEndianBFH.reserved);
  ENCODE(&mImageBufferCurr, littleEndianBFH.dataoffset);
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

  ENCODE(&mImageBufferCurr, littleEndianmBIH.bihsize);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.width);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.height);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.planes);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.bpp);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.compression);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.image_size);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.xppm);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.yppm);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.colors);
  ENCODE(&mImageBufferCurr, littleEndianmBIH.important_colors);

  if (mBMPInfoHeader.bihsize > InfoHeaderLength::WIN_V3) {
    ENCODE(&mImageBufferCurr, littleEndianmBIH.red_mask);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.green_mask);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.blue_mask);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.alpha_mask);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.color_space);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.r.x);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.r.y);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.r.z);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.g.x);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.g.y);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.g.z);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.b.x);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.b.y);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.white_point.b.z);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.gamma_red);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.gamma_green);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.gamma_blue);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.intent);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.profile_offset);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.profile_size);
    ENCODE(&mImageBufferCurr, littleEndianmBIH.reserved);
  }
}
