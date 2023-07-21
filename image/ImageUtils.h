/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageUtils_h
#define mozilla_image_ImageUtils_h

#include "mozilla/Assertions.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
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

using DecodeFramesPromise = MozPromise<size_t, nsresult, false>;

class DecodeImageResult {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  bool IsFinished() const {
    MutexAutoLock lock(mMutex);
    return mIsFinished;
  }

  size_t FramesAvailable() const {
    MutexAutoLock lock(mMutex);
    return mFrames.Length();
  }

  size_t TakeFrames(nsTArray<RefPtr<gfx::SourceSurface>>& aSurfaces) {
    MOZ_ASSERT(aSurfaces.IsEmpty());

    MutexAutoLock lock(mMutex);
    size_t offset = mFrameIndexOffset;
    mFrameIndexOffset += mFrames.Length();
    aSurfaces = std::move(mFrames);
    return offset;
  }

  already_AddRefed<gfx::SourceSurface> GetFrame(size_t aIndex) const {
    MutexAutoLock lock(mMutex);
    if (aIndex < mFrames.Length()) {
      return do_AddRef(mFrames[aIndex]);
    }
    return nullptr;
  }

  already_AddRefed<DecodeFramesPromise> RequestFrames(size_t aIndex) {
    MutexAutoLock lock(mMutex);
    if (aIndex < mFrameIndexOffset) {
      // We already decoded the request frames. We may or may not still have in
      // our buffer.
      return DecodeFramesPromise::CreateAndResolve(0, __func__).forget();
    }
    if (mIsFinished) {
      // There are no more frames to decode.
      return DecodeFramesPromise::CreateAndReject(NS_BASE_STREAM_CLOSED,
                                                  __func__)
          .forget();
    }
    return RequestFramesInternal(aIndex);
  }

 protected:
  DecodeImageResult();
  virtual ~DecodeImageResult();

  virtual already_AddRefed<DecodeFramesPromise> RequestFramesInternal(
      size_t aIndex) = 0;

  mutable Mutex mMutex;
  nsTArray<RefPtr<gfx::SourceSurface>> mFrames MOZ_GUARDED_BY(mMutex);
  size_t mFrameIndexOffset MOZ_GUARDED_BY(mMutex) = 0;
  bool mIsFinished MOZ_GUARDED_BY(mMutex) = false;
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
