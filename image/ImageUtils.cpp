/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/image/ImageUtils.h"
#include "DecodePool.h"
#include "mozilla/gfx/2D.h"

namespace mozilla::image {

DecodeImageResult::DecodeImageResult() = default;

DecodeImageResult::~DecodeImageResult() = default;

class AnonymousDecodeImageResult final : public DecodeImageResult,
                                         public AnonymousDecodingTask {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AnonymousDecodeImageResult, override)

  AnonymousDecodeImageResult(NotNull<Decoder*> aDecoder)
      : DecodeImageResult(),
        AnonymousDecodingTask(aDecoder, /* aResumable */ true),
        mMutex("mozilla::image::AnonymousDecodeImageResult::mMutex") {}

  already_AddRefed<DecodeMetadataPromise> Initialize() override {
    MutexAutoLock lock(mMutex);

    MOZ_ASSERT(mMetadataPromise.IsEmpty());

    RefPtr<DecodeMetadataPromise> p = mMetadataPromise.Ensure(__func__);
    DecodePool::Singleton()->AsyncRun(this);
    return p.forget();
  }

  already_AddRefed<DecodeFramesPromise> DecodeFrames(size_t aCount) override {
    RefPtr<DecodeFramesPromise> p;
    bool dispatch;

    {
      MutexAutoLock lock(mMutex);
      mFramesToDecode = std::max(mFramesToDecode, aCount);
      dispatch = !mMetadataPromise.IsEmpty() && mFramesPromise.IsEmpty();
      p = mFramesPromise.Ensure(__func__);
    }

    if (dispatch) {
      DecodePool::Singleton()->AsyncRun(this);
    }

    return p.forget();
  }

  void Run() override {
    bool decodeMetadata;
    bool decodeFrames;
    size_t framesToDecode;

    {
      MutexAutoLock lock(mMutex);
      decodeMetadata = !mMetadataPromise.IsEmpty();
      decodeFrames = !mFramesPromise.IsEmpty();
      framesToDecode = mFramesToDecode;
    }

    if (decodeMetadata) {
      RefPtr<Decoder> metadataDecoder =
          DecoderFactory::CloneAnonymousMetadataDecoder(mDecoder);
      MOZ_ASSERT(metadataDecoder);
      LexerResult result = metadataDecoder->Decode(WrapNotNull(this));
      MOZ_RELEASE_ASSERT(result.is<TerminalState>());

      MutexAutoLock lock(mMutex);

      if (result == LexerResult(TerminalState::FAILURE)) {
        mMetadataPromise.Reject(NS_ERROR_FAILURE, __func__);
        mFramesPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
        return;
      }

      const auto& mdIn = metadataDecoder->GetImageMetadata();
      const auto size = mdIn.GetSize();
      DecodeMetadataResult mdOut{size.width, size.height, mdIn.GetLoopCount(),
                                 mdIn.HasAnimation()};
      mMetadataPromise.Resolve(mdOut, __func__);
    }

    if (!decodeFrames) {
      return;
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
  MozPromiseHolder<DecodeMetadataPromise> mMetadataPromise;
  MozPromiseHolder<DecodeFramesPromise> mFramesPromise MOZ_GUARDED_BY(mMutex);
  size_t mFramesToDecode MOZ_GUARDED_BY(mMutex) = 1;
};

/* static */ RefPtr<CreateBufferPromise> ImageUtils::CreateSourceBuffer(
    nsIInputStream* aStream) {
  if (NS_WARN_IF(!aStream)) {
    return CreateBufferPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  }

  nsCOMPtr<nsISerialEventTarget> target =
      DecodePool::Singleton()->GetIOEventTarget();
  if (NS_WARN_IF(!target)) {
    return CreateBufferPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                __func__);
  }

  // Ensure the stream is buffered before dispatching.
  nsCOMPtr<nsIInputStream> stream(aStream);
  if (!NS_InputStreamIsBuffered(stream)) {
    nsCOMPtr<nsIInputStream> bufStream;
    nsresult rv = NS_NewBufferedInputStream(getter_AddRefs(bufStream),
                                            stream.forget(), 1024);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return CreateBufferPromise::CreateAndReject(NS_ERROR_INVALID_ARG,
                                                  __func__);
    }
    stream = std::move(bufStream);
  }

  class ReadStreamRunnable final : public Runnable {
   public:
    ReadStreamRunnable(nsCOMPtr<nsIInputStream>&& aStream)
        : Runnable("mozilla::image::ImageUtils::ReadStreamRunnable"),
          mSourceBuffer(new SourceBuffer()),
          mStream(std::move(aStream)) {}

    ~ReadStreamRunnable() override {
      mPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
    }

    RefPtr<CreateBufferPromise> Promise() { return mPromise.Ensure(__func__); }

    NS_IMETHODIMP Run() override {
      MOZ_ASSERT(mSourceBuffer);

      uint64_t length;
      nsresult rv = mStream->Available(&length);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        mPromise.Reject(rv, __func__);
        return NS_OK;
      }

      rv = mSourceBuffer->AppendFromInputStream(mStream, length);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        mPromise.Reject(rv, __func__);
        return NS_OK;
      }

      mPromise.Resolve(std::move(mSourceBuffer), __func__);
      return NS_OK;
    }

   private:
    MozPromiseHolder<CreateBufferPromise> mPromise;
    RefPtr<SourceBuffer> mSourceBuffer;
    nsCOMPtr<nsIInputStream> mStream;
  };

  auto runnable = MakeRefPtr<ReadStreamRunnable>(aStream);
  auto p = runnable->Promise();
  MOZ_ASSERT(p);
  MOZ_ALWAYS_SUCCEEDS(target->Dispatch(runnable.forget()));
  return p;
}

/* static */ RefPtr<CreateBufferPromise> ImageUtils::CreateSourceBuffer(
    const uint8_t* aData, size_t aLength) {
  if (NS_WARN_IF(!aData || aLength == 0)) {
    return CreateBufferPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  }

  auto sourceBuffer = MakeRefPtr<SourceBuffer>();
  nsresult rv =
      sourceBuffer->Append(reinterpret_cast<const char*>(aData), aLength);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return CreateBufferPromise::CreateAndReject(NS_ERROR_OUT_OF_MEMORY,
                                                __func__);
  }

  return CreateBufferPromise::CreateAndResolve(std::move(sourceBuffer),
                                               __func__);
}

/* static */ already_AddRefed<DecodeImageResult> ImageUtils::CreateDecoder(
    SourceBuffer* aSourceBuffer, DecoderType aType,
    SurfaceFlags aSurfaceFlags) {
  if (NS_WARN_IF(!aSourceBuffer)) {
    return nullptr;
  }

  if (NS_WARN_IF(aType == DecoderType::UNKNOWN)) {
    return nullptr;
  }

  RefPtr<Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      aType, WrapNotNull(aSourceBuffer), Nothing(),
      DecoderFlags::IMAGE_IS_TRANSIENT, aSurfaceFlags);
  if (NS_WARN_IF(!decoder)) {
    return nullptr;
  }

  return MakeAndAddRef<AnonymousDecodeImageResult>(WrapNotNull(decoder));
}

/* static */ DecoderType ImageUtils::GetDecoderType(
    const nsACString& aMimeType) {
  return DecoderFactory::GetDecoderType(aMimeType.Data());
}

}  // namespace mozilla::image
