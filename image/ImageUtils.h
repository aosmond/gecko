/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageUtils_h
#define mozilla_image_ImageUtils_h

#include "mozilla/image/SurfaceFlags.h"
#include "mozilla/Assertions.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {
class ErrorResult;

namespace gfx {
class SourceSurface;
}

namespace image {
class SourceBuffer;

/**
 * The type of decoder; this is usually determined from a MIME type using
 * DecoderFactory::GetDecoderType() or ImageUtils::GetDecoderType().
 */
enum class DecoderType {
  PNG,
  GIF,
  JPEG,
  BMP,
  BMP_CLIPBOARD,
  ICO,
  ICON,
  WEBP,
  AVIF,
  JXL,
  UNKNOWN
};

struct DecodeMetadataResult {
  int32_t mWidth = 0;
  int32_t mHeight = 0;
  int32_t mRepetitions = -1;
  int32_t mFrames = 0;
  bool mAnimated = false;
};

struct DecodeFramesResult {
  nsTArray<RefPtr<gfx::SourceSurface>> mSurfaces;
  bool mFinished = false;
};

using DecodeMetadataPromise = MozPromise<DecodeMetadataResult, nsresult, true>;
using DecodeFramesPromise = MozPromise<DecodeFramesResult, nsresult, true>;

class AnonymousDecoder {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual RefPtr<DecodeMetadataPromise> Initialize() = 0;

  virtual RefPtr<DecodeFramesPromise> DecodeFrames(size_t aCount) = 0;

 protected:
  AnonymousDecoder();
  virtual ~AnonymousDecoder();
};

using CreateBufferPromise = MozPromise<RefPtr<SourceBuffer>, nsresult, true>;

class ImageUtils {
 public:
  static RefPtr<CreateBufferPromise> CreateSourceBuffer(
      nsIInputStream* aStream);

  static RefPtr<CreateBufferPromise> CreateSourceBuffer(const uint8_t* aData,
                                                        size_t aLength);

  static already_AddRefed<AnonymousDecoder> CreateDecoder(
      SourceBuffer* aSourceBuffer, DecoderType aType,
      SurfaceFlags aSurfaceFlags);

  static DecoderType GetDecoderType(const nsACString& aMimeType);

 private:
  ImageUtils() = delete;
  ~ImageUtils() = delete;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_ImageUtils_h
