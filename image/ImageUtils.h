/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageUtils_h
#define mozilla_image_ImageUtils_h

#include "mozilla/Assertions.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/image/ImageTypes.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {
class ErrorResult;

namespace gfx {
class SourceSurface;
}

namespace image {

struct DecodeFramesResult {
  nsTArray<RefPtr<gfx::SourceSurface>> mFrames;
  bool mFinished = false;
};

using DecodeFramesPromise = MozPromise<DecodeFramesResult, nsresult, false>;

class DecodeImageResult {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual already_AddRefed<DecodeFramesPromise> DecodeFrames(size_t aCount) = 0;

 protected:
  DecodeImageResult();
  virtual ~DecodeImageResult();
};

using DecodeImagePromise =
    MozPromise<RefPtr<DecodeImageResult>, nsresult, false>;

class ImageUtils {
 public:
  static already_AddRefed<DecodeImagePromise> DecodeAnonymousImage(
      const nsACString& aMimeType, ErrorResult& aRv);

  static DecoderType GetDecoderType(const nsACString& aMimeType);

 private:
  ImageUtils() = delete;
  ~ImageUtils() = delete;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_ImageUtils_h
