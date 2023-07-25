/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/image/ImageUtils.h"
#include "DecodePool.h"
#include "mozilla/gfx/2D.h"

namespace mozilla::image {

AnonymousDecoder::AnonymousDecoder() = default;

AnonymousDecoder::~AnonymousDecoder() = default;

class AnonymousDecoderImpl final : public AnonymousDecoder,
                                   public IDecodingTask {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AnonymousDecoderImpl, override)

  AnonymousDecoderImpl(RefPtr<Decoder>&& aDecoder)
      : AnonymousDecoder(),
        mMutex("mozilla::image::AnonymousDecoderImpl::mMutex"),
        mFullDecoder(std::move(aDecoder)),
        mMetadataDecoder(
            DecoderFactory::CloneAnonymousMetadataDecoder(mFullDecoder)) {}

  RefPtr<DecodeMetadataPromise> Initialize() override {
    MutexAutoLock lock(mMutex);

    MOZ_ASSERT(mMetadataPromise.IsEmpty());
    MOZ_ASSERT(mMetadataDecoder);

    RefPtr<DecodeMetadataPromise> p = mMetadataPromise.Ensure(__func__);
    DecodePool::Singleton()->AsyncRun(this);
    return p;
  }

  RefPtr<DecodeFramesPromise> DecodeFrames(size_t aCount) override {
    RefPtr<DecodeFramesPromise> p;
    bool dispatch;

    {
      MutexAutoLock lock(mMutex);
      mFramesToDecode = std::max(mFramesToDecode, aCount);
      dispatch = mMetadataPromise.IsEmpty() && mFramesPromise.IsEmpty();
      p = mFramesPromise.Ensure(__func__);
    }

    if (dispatch) {
      DecodePool::Singleton()->AsyncRun(this);
    }

    return p;
  }

  bool ShouldPreferSyncRun() const override { return false; }

  TaskPriority Priority() const override { return TaskPriority::eLow; }

  void Run() override {
    RefPtr<Decoder> metadataDecoder;
    RefPtr<Decoder> fullDecoder;
    DecodeFramesResult result;

    {
      MutexAutoLock lock(mMutex);
      if (!mMetadataPromise.IsEmpty()) {
        metadataDecoder = mMetadataDecoder;
      }
      if (!mFramesPromise.IsEmpty()) {
        fullDecoder = mFullDecoder;
        result = std::move(mPendingFramesResult);
      }
    }

    // We can only run the full decoder for frames once we have a successful
    // metadata decode.
    if (metadataDecoder && !DoMetadataDecode(metadataDecoder)) {
      MOZ_ASSERT(result.mFrames.IsEmpty());
      return;
    }

    if (fullDecoder) {
      DoFrameDecode(fullDecoder, std::move(result));
    }

    MOZ_ASSERT(result.mFrames.IsEmpty());
  }

 private:
  bool DoMetadataDecode(Decoder* aDecoder) {
    LexerResult result = aDecoder->Decode(WrapNotNull(this));
    if (!result.is<TerminalState>()) {
      MOZ_ASSERT(result == LexerResult(Yield::NEED_MORE_DATA));
      return false;
    }

    MutexAutoLock lock(mMutex);

    if (result == LexerResult(TerminalState::FAILURE)) {
      mMetadataPromise.Reject(NS_ERROR_FAILURE, __func__);
      mFramesPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
      mFullDecoder = nullptr;
      mMetadataDecoder = nullptr;
      return false;
    }

    const auto& mdIn = aDecoder->GetImageMetadata();
    const auto size = mdIn.GetSize();
    DecodeMetadataResult mdOut{size.width, size.height, mdIn.GetLoopCount(),
                               /* FIXME mFrames */ 1, mdIn.HasAnimation()};
    mMetadataPromise.Resolve(mdOut, __func__);
    mMetadataDecoder = nullptr;
    return true;
  }

  void DoFrameDecode(Decoder* aDecoder, DecodeFramesResult&& aResult) {
    while (true) {
      LexerResult result = aDecoder->Decode(WrapNotNull(this));

      // We haven't finished populating the SourceBuffer. Once we have more
      // data, we will autmatically get resumed.
      if (result == LexerResult(Yield::NEED_MORE_DATA)) {
        MutexAutoLock lock(mMutex);
        mPendingFramesResult = std::move(aResult);
        break;
      }

      // We may have a new frame to process, whether it is the terminal state or
      // not.
      RefPtr<imgFrame> frame = aDecoder->GetCurrentFrame();
      if (frame) {
        RefPtr<gfx::SourceSurface> surface = frame->GetSourceSurface();
        if (surface) {
          if (aResult.mFrames.IsEmpty() ||
              aResult.mFrames.LastElement().mSurface != surface) {
            aResult.mFrames.AppendElement(
                DecodedFrame{std::move(surface), frame->GetTimeout()});
          }
        } else {
          MOZ_ASSERT_UNREACHABLE("No surface from frame?");
        }
      }

      // If we reached a terminal state, then there will be no more frames.
      if (result.is<TerminalState>()) {
        aResult.mFinished = true;

        MutexAutoLock lock(mMutex);
        mFramesToDecode = 0;
        mFramesPromise.Resolve(std::move(aResult), __func__);
        break;
      }

      // We got a single frame from the pipeline.
      MOZ_ASSERT(result == LexerResult(Yield::OUTPUT_AVAILABLE));

      MutexAutoLock lock(mMutex);
      if (mFramesToDecode >= aResult.mFrames.Length()) {
        mFramesToDecode = 0;
        mFramesPromise.Resolve(std::move(aResult), __func__);
        break;
      }
    }
  }

  virtual ~AnonymousDecoderImpl() {
    MutexAutoLock lock(mMutex);
    mFramesPromise.RejectIfExists(NS_ERROR_ABORT, __func__);
  }

  Mutex mMutex;
  MozPromiseHolder<DecodeMetadataPromise> mMetadataPromise;
  MozPromiseHolder<DecodeFramesPromise> mFramesPromise MOZ_GUARDED_BY(mMutex);
  RefPtr<Decoder> mFullDecoder MOZ_GUARDED_BY(mMutex);
  RefPtr<Decoder> mMetadataDecoder MOZ_GUARDED_BY(mMutex);
  DecodeFramesResult mPendingFramesResult MOZ_GUARDED_BY(mMutex);
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

      mSourceBuffer->Complete(NS_OK);
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

  sourceBuffer->Complete(NS_OK);
  return CreateBufferPromise::CreateAndResolve(std::move(sourceBuffer),
                                               __func__);
}

/* static */ already_AddRefed<AnonymousDecoder> ImageUtils::CreateDecoder(
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

  return MakeAndAddRef<AnonymousDecoderImpl>(std::move(decoder));
}

/* static */ DecoderType ImageUtils::GetDecoderType(
    const nsACString& aMimeType) {
  return DecoderFactory::GetDecoderType(aMimeType.Data());
}

}  // namespace mozilla::image
