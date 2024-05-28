/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageDecoder.h"
#include <algorithm>
#include <cstdint>
#include "ImageContainer.h"
#include "ImageDecoderReadRequest.h"
#include "nsComponentManagerUtils.h"
#include "nsTHashSet.h"
#include "mozilla/dom/ImageTrack.h"
#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/image/ImageUtils.h"
#include "mozilla/image/SourceBuffer.h"
#include "mozilla/Logging.h"

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ImageDecoder)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ImageDecoder)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTracks)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReadRequest)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCompletePromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOutstandingDecodes)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDecodedFrames)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ImageDecoder)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTracks)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReadRequest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCompletePromise)
  for (uint32_t i = 0; i < tmp->mOutstandingDecodes.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOutstandingDecodes[i].mPromise);
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDecodedFrames)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageDecoder)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageDecoder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageDecoder)

ImageDecoder::ImageDecoder(nsCOMPtr<nsIGlobalObject>&& aParent,
                           const nsAString& aType)
    : mParent(std::move(aParent)),
      mType(aType),
      mFramesTimestamp(image::FrameTimeout::Zero()) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p ImageDecoder", this));
}

ImageDecoder::~ImageDecoder() {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p ~ImageDecoder", this));
  Destroy();
}

JSObject* ImageDecoder::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageDecoder_Binding::Wrap(aCx, this, aGivenProto);
}

void ImageDecoder::Destroy() {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("ImageDecoder %p Destroy", this));

  if (mDecoder) {
    mDecoder->Destroy();
  }

  if (mReadRequest) {
    mReadRequest->Destroy();
    mReadRequest = nullptr;
  }

  if (mTracks) {
    mTracks->Destroy();
    mTracks = nullptr;
  }

  mSourceBuffer = nullptr;
  mDecoder = nullptr;
  mParent = nullptr;
}

/* static */ already_AddRefed<ImageDecoder> ImageDecoder::Constructor(
    const GlobalObject& aGlobal, const ImageDecoderInit& aInit,
    ErrorResult& aRv) {
  // 10.2.2.1. If init is not valid ImageDecoderInit, throw a TypeError.
  // 10.3.1. If type is not a valid image MIME type, return false.
  const auto mimeType = Substring(aInit.mType, 0, 6);
  if (!mimeType.Equals(u"image/"_ns)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder Constructor -- bad mime type"));
    aRv.ThrowTypeError("Invalid MIME type, must be 'image'");
    return nullptr;
  }

  RefPtr<ImageDecoderReadRequest> readRequest;

  if (aInit.mData.IsReadableStream()) {
    const auto& stream = aInit.mData.GetAsReadableStream();
    // 10.3.2. If data is of type ReadableStream and the ReadableStream is
    // disturbed or locked, return false.
    if (stream->Disturbed() || stream->Locked()) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Error,
              ("ImageDecoder Constructor -- bad stream"));
      aRv.ThrowTypeError("ReadableStream data is disturbed and/or locked");
      return nullptr;
    }
  } else {
    // 10.3.3. If data is of type BufferSource:
    bool empty;
    if (aInit.mData.IsArrayBufferView()) {
      const auto& view = aInit.mData.GetAsArrayBufferView();
      empty = view.ProcessData(
          [](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
            return aData.IsEmpty();
          });
    } else if (aInit.mData.IsArrayBuffer()) {
      const auto& buffer = aInit.mData.GetAsArrayBuffer();
      empty = buffer.ProcessData(
          [](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
            return aData.IsEmpty();
          });
    } else {
      MOZ_ASSERT_UNREACHABLE("Unsupported data type!");
      aRv.ThrowNotSupportedError("Unsupported data type");
      return nullptr;
    }

    // 10.3.3.1. If data is [detached], return false.
    // 10.3.3.2. If data is empty, return false.
    if (empty) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Error,
              ("ImageDecoder Constructor -- detached/empty BufferSource"));
      aRv.ThrowTypeError("BufferSource is detached/empty");
      return nullptr;
    }
  }

  // 10.3.4. If desiredWidth exists and desiredHeight does not exist, return
  // false.
  // 10.3.5. If desiredHeight exists and desiredWidth does not exist, return
  // false.
  if (aInit.mDesiredHeight.WasPassed() != aInit.mDesiredWidth.WasPassed()) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder Constructor -- both/neither desiredHeight/width "
             "needed"));
    aRv.ThrowTypeError(
        "Both or neither of desiredHeight and desiredWidth must be passed");
    return nullptr;
  }

  nsTHashSet<const ArrayBuffer*> transferSet;
  for (const auto& buffer : aInit.mTransfer) {
    // 10.2.2.2. If init.transfer contains more than one reference to the same
    // ArrayBuffer, then throw a DataCloneError DOMException.
    if (transferSet.Contains(&buffer)) {
      MOZ_LOG(
          gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder Constructor -- duplicate transferred ArrayBuffer"));
      aRv.ThrowDataCloneError(
          "Transfer contains duplicate ArrayBuffer objects");
      return nullptr;
    }
    transferSet.Insert(&buffer);
    // 10.2.2.3.1. If [[Detached]] internal slot is true, then throw a
    // DataCloneError DOMException.
    bool empty = buffer.ProcessData(
        [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
          return aData.IsEmpty();
        });
    if (empty) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Error,
              ("ImageDecoder Constructor -- empty/detached transferred "
               "ArrayBuffer"));
      aRv.ThrowDataCloneError(
          "Transfer contains empty/detached ArrayBuffer objects");
      return nullptr;
    }
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  auto imageDecoder = MakeRefPtr<ImageDecoder>(std::move(global), aInit.mType);
  imageDecoder->Initialize(aGlobal, aInit, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder Constructor -- initialize failed"));
    return nullptr;
  }

  return imageDecoder.forget();
}

/* static */ already_AddRefed<Promise> ImageDecoder::IsTypeSupported(
    const GlobalObject& aGlobal, const nsAString& aType, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  const auto subType = Substring(aType, 0, 6);
  if (!subType.Equals(u"image/"_ns)) {
    promise->MaybeRejectWithTypeError("Invalid MIME type, must be 'image'"_ns);
    return promise.forget();
  }

  NS_ConvertUTF16toUTF8 mimeType(aType);
  image::DecoderType type = image::ImageUtils::GetDecoderType(mimeType);
  promise->MaybeResolve(type != image::DecoderType::UNKNOWN);
  return promise.forget();
}

void ImageDecoder::Initialize(const GlobalObject& aGlobal,
                              const ImageDecoderInit& aInit,
                              ErrorResult& aRv) {
  mCompletePromise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Initialize -- create promise failed", this));
    return;
  }

  // 10.2.2.8. Assign [[ImageTrackList]] a new ImageTrackList initialized as
  // follows:
  // 10.2.2.8.1. Assign a new list to [[track list]].
  mTracks = MakeAndAddRef<ImageTrackList>(mParent, this);
  mTracks->Initialize(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Initialize -- create tracks failed", this));
    return;
  }

  NS_ConvertUTF16toUTF8 mimeType(mType);
  image::DecoderType type = image::ImageUtils::GetDecoderType(mimeType);

  image::SurfaceFlags surfaceFlags = image::DefaultSurfaceFlags();
  switch (aInit.mColorSpaceConversion) {
    case ColorSpaceConversion::None:
      surfaceFlags |= image::SurfaceFlags::NO_COLORSPACE_CONVERSION;
      break;
    case ColorSpaceConversion::Default:
      break;
    default:
      MOZ_LOG(
          gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p Initialize -- unsupported colorspace conversion",
           this));
      aRv.ThrowNotSupportedError("Unsupported colorspace conversion");
      return;
  }

  mSourceBuffer = MakeRefPtr<image::SourceBuffer>();
  mDecoder =
      image::ImageUtils::CreateDecoder(mSourceBuffer, type, surfaceFlags);
  if (NS_WARN_IF(!mDecoder)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Initialize -- failed to create platform decoder",
             this));
    OnMetadataFailed(NS_ERROR_FAILURE);
    return;
  }

  const auto fnSourceBufferFromSpan = [&](const Span<uint8_t>& aData) {
    nsresult rv = mSourceBuffer->ExpectLength(aData.Length());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(
          gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p Initialize -- failed to pre-allocate source buffer",
           this));
      aRv.ThrowRangeError("Could not allocate for encoded source buffer");
      return;
    }

    rv = mSourceBuffer->Append(reinterpret_cast<const char*>(aData.Elements()),
                               aData.Length());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Error,
              ("ImageDecoder %p Initialize -- failed to append source buffer",
               this));
      aRv.ThrowRangeError("Could not allocate for encoded source buffer");
      return;
    }

    mSourceBuffer->Complete(NS_OK);
  };

  if (aInit.mData.IsReadableStream()) {
    const auto& stream = aInit.mData.GetAsReadableStream();
    mReadRequest = MakeAndAddRef<ImageDecoderReadRequest>(mSourceBuffer);
    if (NS_WARN_IF(!mReadRequest->Initialize(aGlobal, this, stream))) {
      MOZ_LOG(
          gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p Initialize -- create read request failed", this));
      aRv.ThrowInvalidStateError("Could not create reader for ReadableStream");
      return;
    }
  } else if (aInit.mData.IsArrayBufferView()) {
    const auto& view = aInit.mData.GetAsArrayBufferView();
    view.ProcessData([&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
      return fnSourceBufferFromSpan(aData);
    });
    if (aRv.Failed()) {
      return;
    }
  } else if (aInit.mData.IsArrayBuffer()) {
    const auto& buffer = aInit.mData.GetAsArrayBuffer();
    buffer.ProcessData(
        [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
          return fnSourceBufferFromSpan(aData);
        });
    if (aRv.Failed()) {
      return;
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("Unsupported data type!");
    aRv.ThrowNotSupportedError("Unsupported data type");
    return;
  }

  // 10.2.2.18.7. Queue a control message to decode track metadata.
  mDecoder->DecodeMetadata()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = WeakPtr{this}](const image::DecodeMetadataResult& aMetadata) {
        if (self) {
          self->OnMetadataSuccess(aMetadata);
        }
      },
      [self = WeakPtr{this}](const nsresult& aErr) {
        if (self) {
          self->OnMetadataFailed(aErr);
        }
      });
}

void ImageDecoder::OnSourceBufferComplete(nsresult aErr) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnSourceBufferComplete -- success %d", this,
           NS_SUCCEEDED(aErr)));

  MOZ_ASSERT(mSourceBuffer->IsComplete());

  if (NS_WARN_IF(NS_FAILED(aErr))) {
    OnCompleteFailed(aErr);
    return;
  }

  OnCompleteSuccess();
}

void ImageDecoder::OnCompleteSuccess() {
  if (mComplete) {
    return;
  }

  // There are two conditions we need to fulfill before we are complete:
  //
  // 10.2.1. Internal Slots - [[complete]]
  // A boolean indicating whether [[encoded data]] is completely buffered.
  //
  // 10.6.1. Internal Slots - [[ready promise]]
  // NOTE: ImageTrack frameCount can receive subsequent updates until complete
  // is true.
  if (!mSourceBuffer->IsComplete() || !mHasFrameCount) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
            ("ImageDecoder %p OnCompleteSuccess -- not complete yet; "
             "sourceBuffer %d, hasFrameCount %d",
             this, mSourceBuffer->IsComplete(), mHasFrameCount));
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnCompleteSuccess -- complete", this));
  mComplete = true;
  mCompletePromise->MaybeResolveWithUndefined();
}

void ImageDecoder::OnCompleteFailed(nsresult aErr) {
  if (mComplete) {
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p OnCompleteFailed -- complete", this));
  mComplete = true;
  mCompletePromise->MaybeRejectWithInvalidStateError(""_ns);
}

void ImageDecoder::OnMetadataSuccess(
    const image::DecodeMetadataResult& aMetadata) {
  if (!mTracks) {
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnMetadataSuccess -- %dx%d, repetitions %d, "
           "animated %d",
           this, aMetadata.mWidth, aMetadata.mHeight, aMetadata.mRepetitions,
           aMetadata.mAnimated));

  mTracks->OnMetadataSuccess(aMetadata);

  if (aMetadata.mAnimated) {
    RequestFrameCount(/* aKnownFrameCount */ 0);
  } else {
    OnFrameCountSuccess(image::DecodeFrameCountResult{/* aFrameCount */ 1,
                                                      /* mFinished */ true});
  }
}

void ImageDecoder::OnMetadataFailed(const nsresult& aErr) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p OnMetadataFailed", this));

  if (mTracks) {
    mTracks->OnMetadataFailed(aErr);
  }

  OnCompleteFailed(aErr);
  OnDecodeFramesFailed(aErr);
}

void ImageDecoder::RequestFrameCount(uint32_t aKnownFrameCount) {
  MOZ_ASSERT(!mHasFrameCount);

  if (NS_WARN_IF(!mDecoder)) {
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p RequestFrameCount -- knownFrameCount %u", this,
           aKnownFrameCount));
  mDecoder->DecodeFrameCount(aKnownFrameCount)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = WeakPtr{this}](const image::DecodeFrameCountResult& aResult) {
            if (self) {
              self->OnFrameCountSuccess(aResult);
            }
          },
          [self = WeakPtr{this}](const nsresult& aErr) {
            if (self) {
              self->OnFrameCountFailed(aErr);
            }
          });
}

void ImageDecoder::RequestDecodeFrames(uint32_t aFrameIndex) {
  if (NS_WARN_IF(aFrameIndex < mDecodedFrames.Length())) {
    MOZ_ASSERT_UNREACHABLE("Already decoded requested frame!");
    return;
  }

  if (NS_WARN_IF(!mDecoder)) {
    return;
  }

  MOZ_ASSERT(!mOutstandingDecodes.IsEmpty());

  CheckedInt<uint32_t> framesToDecode =
      CheckedInt<uint32_t>(aFrameIndex) + 1 - mDecodedFrames.Length();
  if (NS_WARN_IF(!framesToDecode.isValid())) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p RequestDecodeFrames -- frameIndex %u overflow",
             this, aFrameIndex));
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p RequestDecodeFrames -- frameIndex %u "
           "(framesToDecode %u)",
           this, aFrameIndex, framesToDecode.value()));
  mDecoder->DecodeFrames(framesToDecode.value())
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = WeakPtr{this}](const image::DecodeFramesResult& aResult) {
            if (self) {
              self->OnDecodeFramesSuccess(aResult);
            }
          },
          [self = WeakPtr{this}](const nsresult& aErr) {
            if (self) {
              self->OnDecodeFramesFailed(aErr);
            }
          });
}

void ImageDecoder::OnFrameCountSuccess(
    const image::DecodeFrameCountResult& aResult) {
  if (!mTracks) {
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnFrameCountSuccess -- frameCount %u, finished %d",
           this, aResult.mFrameCount, aResult.mFinished));

  mTracks->OnFrameCountSuccess(aResult);

  if (aResult.mFinished) {
    mHasFrameCount = true;
    OnCompleteSuccess();
  } else {
    RequestFrameCount(aResult.mFrameCount);
  }

  if (mOutstandingDecodes.IsEmpty()) {
    return;
  }

  AutoTArray<OutstandingDecode, 1> rejected;

  ImageTrack* track = mTracks->GetSelectedTrack();
  if (NS_WARN_IF(!track)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p OnFrameCountSuccess -- no selected track", this));
    rejected = std::move(mOutstandingDecodes);
    for (const auto& i : rejected) {
      i.mPromise->MaybeRejectWithInvalidStateError("No track selected"_ns);
    }
    return;
  }

  if (NS_WARN_IF(!mDecoder)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p OnFrameCountSuccess -- no decoder", this));
    rejected = std::move(mOutstandingDecodes);
    for (const auto& i : rejected) {
      i.mPromise->MaybeRejectWithInvalidStateError("No decoder available"_ns);
    }
    return;
  }

  uint32_t minFrameIndex = UINT32_MAX;

  for (uint32_t i = 0; i < mOutstandingDecodes.Length();) {
    auto& decode = mOutstandingDecodes[i];
    const auto frameIndex = decode.mFrameIndex;
    if (frameIndex < track->FrameCount()) {
      minFrameIndex = std::min(minFrameIndex, frameIndex);
      ++i;
    } else if (track->FrameCountComplete()) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Warning,
              ("ImageDecoder %p OnFrameCountSuccess -- reject %u out-of-bounds",
               this, frameIndex));
      rejected.AppendElement(std::move(decode));
      mOutstandingDecodes.RemoveElementAt(i);
    } else {
      ++i;
    }
  }

  if (!mOutstandingDecodes.IsEmpty()) {
    RequestDecodeFrames(minFrameIndex);
  }

  for (const auto& i : rejected) {
    i.mPromise->MaybeRejectWithRangeError("Index beyond frame count bounds"_ns);
  }
}

void ImageDecoder::OnFrameCountFailed(const nsresult& aErr) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p OnFrameCountFailed", this));
  OnCompleteFailed(aErr);
}

void ImageDecoder::GetType(nsAString& aType) const { aType.Assign(mType); }

already_AddRefed<Promise> ImageDecoder::Decode(
    const ImageDecodeOptions& aOptions, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Decode -- create promise failed", this));
    return nullptr;
  }

  // 10.2.4.1. If [[closed]] is true, return a Promise rejected with an
  // InvalidStateError DOMException.
  if (!mTracks || !mDecoder) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Decode -- closed", this));
    promise->MaybeRejectWithInvalidStateError("Closed decoder"_ns);
    return promise.forget();
  }

  // 10.2.4.2. If [[ImageTrackList]]'s [[selected index]] is '-1', return a
  // Promise rejected with an InvalidStateError DOMException.
  ImageTrack* track = mTracks->GetSelectedTrack();
  if (!track) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Decode -- no track selected", this));
    promise->MaybeRejectWithInvalidStateError("No track selected"_ns);
    return promise.forget();
  }

  if (NS_WARN_IF(aOptions.mFrameIndex > INT32_MAX)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Decode -- invalid index", this));
    promise->MaybeRejectWithRangeError("Index outside valid range"_ns);
    return promise.forget();
  }

  if (mComplete && aOptions.mFrameIndex >= track->FrameCount()) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Decode -- index %u out-of-bounds %u", this,
             aOptions.mFrameIndex, track->FrameCount()));
    promise->MaybeRejectWithRangeError("Index beyond frame count bounds"_ns);
    return promise.forget();
  }

  if (aOptions.mFrameIndex < mDecodedFrames.Length()) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
            ("ImageDecoder %p Decode -- index %u already decoded", this,
             aOptions.mFrameIndex));
    ImageDecodeResult result;
    result.mImage = mDecodedFrames[aOptions.mFrameIndex];
    result.mComplete = true;
    promise->MaybeResolve(result);
    return promise.forget();
  }

  MOZ_LOG(
      gWebCodecsLog, LogLevel::Debug,
      ("ImageDecoder %p Decode -- queue index %u", this, aOptions.mFrameIndex));
  bool wasEmpty = mOutstandingDecodes.IsEmpty();
  mOutstandingDecodes.AppendElement(
      OutstandingDecode{promise, aOptions.mFrameIndex});

  if (wasEmpty) {
    RequestDecodeFrames(aOptions.mFrameIndex);
  }

  return promise.forget();
}

void ImageDecoder::OnDecodeFramesSuccess(
    const image::DecodeFramesResult& aResult) {
  if (!mTracks) {
    return;
  }

  mDecodedFrames.SetCapacity(mDecodedFrames.Length() +
                             aResult.mFrames.Length());

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnDecodeFramesSuccess -- decoded %zu frames, %zu "
           "total, finished %d",
           this, aResult.mFrames.Length(), mDecodedFrames.Length(),
           aResult.mFinished));

  for (const auto& f : aResult.mFrames) {
    VideoColorSpaceInit colorSpace;
    gfx::IntSize size = f.mSurface->GetSize();
    gfx::IntRect rect(gfx::IntPoint(0, 0), size);

    Maybe<VideoPixelFormat> format =
        SurfaceFormatToVideoPixelFormat(f.mSurface->GetFormat());
    MOZ_ASSERT(format, "Unexpected format for image!");

    Maybe<uint64_t> duration;
    if (f.mTimeout != image::FrameTimeout::Forever()) {
      duration =
          Some(static_cast<uint64_t>(f.mTimeout.AsMilliseconds()) * 1000);
    }

    uint64_t timestamp = UINT64_MAX;
    if (mFramesTimestamp != image::FrameTimeout::Forever()) {
      timestamp =
          static_cast<uint64_t>(mFramesTimestamp.AsMilliseconds()) * 1000;
    }

    mFramesTimestamp += f.mTimeout;

    auto image = MakeRefPtr<layers::SourceSurfaceImage>(size, f.mSurface);
    auto frame = MakeRefPtr<VideoFrame>(mParent, image, format, size, rect,
                                        size, duration, timestamp, colorSpace);
    mDecodedFrames.AppendElement(std::move(frame));
  }

  AutoTArray<OutstandingDecode, 4> resolved;
  AutoTArray<OutstandingDecode, 4> rejected;
  uint32_t minFrameIndex = UINT32_MAX;

  for (uint32_t i = 0; i < mOutstandingDecodes.Length();) {
    auto& decode = mOutstandingDecodes[i];
    const auto frameIndex = decode.mFrameIndex;
    if (frameIndex < mDecodedFrames.Length()) {
      MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
              ("ImageDecoder %p OnDecodeFramesSuccess -- resolved index %u",
               this, frameIndex));
      resolved.AppendElement(std::move(decode));
      mOutstandingDecodes.RemoveElementAt(i);
    } else if (aResult.mFinished) {
      // We have gotten the last frame from the decoder, so we must reject any
      // unfulfilled requests.
      MOZ_LOG(gWebCodecsLog, LogLevel::Warning,
              ("ImageDecoder %p OnDecodeFramesSuccess -- rejected index %u "
               "out-of-bounds",
               this, frameIndex));
      rejected.AppendElement(std::move(decode));
      mOutstandingDecodes.RemoveElementAt(i);
    } else {
      // We haven't gotten the last frame yet, so we can advance to the next
      // one.
      minFrameIndex = std::min(minFrameIndex, frameIndex);
      ++i;
    }
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnDecodeFramesSuccess -- outstanding decodes %zu",
           this, mOutstandingDecodes.Length()));

  if (!mOutstandingDecodes.IsEmpty()) {
    RequestDecodeFrames(minFrameIndex);
  }

  for (const auto& i : resolved) {
    ImageDecodeResult result;
    result.mImage = mDecodedFrames[i.mFrameIndex];
    result.mComplete = aResult.mFinished;
    i.mPromise->MaybeResolve(result);
  }

  for (const auto& i : rejected) {
    i.mPromise->MaybeRejectWithRangeError("No more frames available"_ns);
  }
}

void ImageDecoder::OnDecodeFramesFailed(const nsresult& aErr) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p OnDecodeFramesFailed", this));

  AutoTArray<OutstandingDecode, 1> rejected = std::move(mOutstandingDecodes);
  for (const auto& i : rejected) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p OnDecodeFramesFailed -- reject index %u", this,
             i.mFrameIndex));
    i.mPromise->MaybeRejectWithRangeError("No more frames available"_ns);
  }
}

void ImageDecoder::Reset() {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("ImageDecoder %p Reset", this));
  // 10.2.5. Reset ImageDecoder (with exception)

  // 1. Signal [[codec implementation]] to abort any active decoding operation.
  if (mDecoder) {
    mDecoder->CancelDecodeFrames();
  }

  // 2. For each decodePromise in [[pending decode promises]]:
  // 2.1. Reject decodePromise with exception.
  // 2.3. Remove decodePromise from [[pending decode promises]].
  AutoTArray<OutstandingDecode, 1> rejected = std::move(mOutstandingDecodes);
  for (const auto& i : rejected) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
            ("ImageDecoder %p Reset -- reject index %u", this, i.mFrameIndex));
    i.mPromise->MaybeRejectWithAbortError("Reset decoder"_ns);
  }
}

void ImageDecoder::Close() {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("ImageDecoder %p Close", this));

  // 10.2.5. Algorithms - Close ImageDecoder (with exception)

  // 1. Run the Reset ImageDecoder algorithm with exception.
  if (mDecoder) {
    mDecoder->Destroy();
  }

  AutoTArray<OutstandingDecode, 1> rejected = std::move(mOutstandingDecodes);
  for (const auto& i : rejected) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
            ("ImageDecoder %p Close -- reject index %u", this, i.mFrameIndex));
    i.mPromise->MaybeRejectWithAbortError("Closed decoder"_ns);
  }

  // 3. Clear [[codec implementation]] and release associated system resources.
  mDecodedFrames.Clear();

  if (mReadRequest) {
    mReadRequest->Destroy();
    mReadRequest = nullptr;
  }

  mSourceBuffer = nullptr;
  mDecoder = nullptr;

  // 4. Remove all entries from [[ImageTrackList]].
  // 5. Assign -1 to [[ImageTrackList]]'s [[selected index]].
  if (mTracks) {
    mTracks->Destroy();
    mTracks = nullptr;
  }

  if (!mComplete) {
    mCompletePromise->MaybeRejectWithAbortError("Closed decoder"_ns);
    mComplete = true;
  }
}

}  // namespace mozilla::dom
