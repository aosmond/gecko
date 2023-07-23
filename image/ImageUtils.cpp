/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/image/ImageUtils.h"
#include "mozilla/gfx/2D.h"

namespace mozilla::image {

DecodeImageResult::DecodeImageResult() = default;

DecodeImageResult::~DecodeImageResult() = default;

class AnonymousDecodeImageResult final : public DecodeImageResult,
                                         public AnonymousDecodingTask {
 public:
  AnonymousDecodeImageResult(NotNull<Decoder*> aDecoder)
      : DecodeImageResult(),
        AnonymousDecodingTask(aDecoder, /* aResumable */ true),
        mMutex("mozilla::image::AnonymousDecodeImageResult::mMutex") {}

  already_AddRefed<DecodeFramesPromise> DecodeFrames(size_t aCount) override {
    RefPtr<DecodeFramesPromise> p;
    bool dispatch;

    {
      MutexAutoLock lock(mMutex);
      mFramesToDecode = std::max(mFramesToDecode, aCount);
      dispatch = mFramesPromise.IsEmpty();
      p = mFramesPromise.Ensure(__func__);
    }

    if (dispatch) {
      DecodePool::Singleton()->AsyncRun(this);
    }

    return p;
  }

  void Run() override {
    size_t framesToDecode;
    {
      MutexAutoLock lock(mMutex);
      if (NS_WARN_IF(mFramesPromise.IsEmpty())) {
        return;
      }
      framesToDecode = mFramesToDecode;
    }

    DecodeFramesResult result;

    while (true) {
      result.mFinished = DoDecode(framesToDecode);
      TakeSurfaces(result.mSurfaces);

      {
        MutexAutoLock lock(mMutex);
        MOZ_ASSERT(!mFramesPromise.IsEmpty());

        if (result.mFinished || mFramesToDecode >= result.mSurfaces.Length()) {
          mFramesToDecode = 0;
          mFramesPromise.Resolve(std::move(result), __func__);
          return;
        }
      }
    }
  }

 private:
  virtual ~AnonymousDecodeImageResult() {
    MutexAutoLock lock(mMutex);
    mFramesPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
  }

  Mutex mMutex;
  MozPromiseHolder<DecodeFramesPromise> mFramesPromise MOZ_GUARDED_BY(mMutex);
  size_t mFramesToDecode MOZ_GUARDED_BY(mMutex) = 1;
};

/* static */ already_AddRefed<DecodeImagePromise>
ImageUtils::DecodeAnonymousImage(const nsACString& aMimeType,
                                 ErrorResult& aRv) {
  return nullptr;
}

/* static */ DecoderType ImageUtils::GetDecoderType(
    const nsACString& aMimeType) {
  return DecoderFactory::GetDecoderType(aMimeType.Data());
}

}  // namespace mozilla::image
