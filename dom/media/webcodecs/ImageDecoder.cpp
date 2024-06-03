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
#include "MediaResult.h"
#include "mozilla/dom/ImageTrack.h"
#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/image/ImageUtils.h"
#include "mozilla/image/SourceBuffer.h"
#include "mozilla/Logging.h"
#include "nsComponentManagerUtils.h"
#include "nsTHashSet.h"

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

class ImageDecoder::ControlMessage {
 public:
  ControlMessage() = default;
  virtual ~ControlMessage() = default;

  virtual ConfigureMessage* AsConfigureMessage() { return nullptr; }
  virtual DecodeMetadataMessage* AsDecodeMetadataMessage() { return nullptr; }
  virtual DecodeFrameMessage* AsDecodeFrameMessage() { return nullptr; }
  virtual SelectTrackMessage* AsSelectTrackMessage() { return nullptr; }
};

class ImageDecoder::ConfigureMessage final
    : public ImageDecoder::ControlMessage {
 public:
  explicit ConfigureMessage(ColorSpaceConversion aColorSpaceConversion)
      : mColorSpaceConversion(aColorSpaceConversion) {}

  ConfigureMessage* AsConfigureMessage() override { return this; }

  const ColorSpaceConversion mColorSpaceConversion;
};

class ImageDecoder::DecodeMetadataMessage final
    : public ImageDecoder::ControlMessage {
 public:
  DecodeMetadataMessage* AsDecodeMetadataMessage() override { return this; }
};

class ImageDecoder::DecodeFrameMessage final
    : public ImageDecoder::ControlMessage {
 public:
  DecodeFrameMessage(uint32_t aFrameIndex, bool aCompleteFramesOnly)
      : mFrameIndex(aFrameIndex), mCompleteFramesOnly(aCompleteFramesOnly) {}

  DecodeFrameMessage* AsDecodeFrameMessage() override { return this; }

  const uint32_t mFrameIndex;
  const bool mCompleteFramesOnly;
};

class ImageDecoder::SelectTrackMessage final
    : public ImageDecoder::ControlMessage {
 public:
  SelectTrackMessage(uint32_t aSelectedTrack)
      : mSelectedTrack(aSelectedTrack) {}

  SelectTrackMessage* AsSelectTrackMessage() override { return this; }

  const uint32_t mSelectedTrack;
};

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

void ImageDecoder::QueueConfigureMessage(
    ColorSpaceConversion aColorSpaceConversion) {
  mControlMessageQueue.push(
      MakeUnique<ConfigureMessage>(aColorSpaceConversion));
}

void ImageDecoder::QueueDecodeMetadataMessage() {
  mControlMessageQueue.push(MakeUnique<DecodeMetadataMessage>());
}

void ImageDecoder::QueueDecodeFrameMessage(uint32_t aFrameIndex,
                                           bool aCompleteFramesOnly) {
  mControlMessageQueue.push(
      MakeUnique<DecodeFrameMessage>(aFrameIndex, aCompleteFramesOnly));
}

void ImageDecoder::QueueSelectTrackMessage(uint32_t aSelectedIndex) {
  mControlMessageQueue.push(MakeUnique<SelectTrackMessage>(aSelectedIndex));
}

void ImageDecoder::ResumeControlMessageQueue() {
  MOZ_ASSERT(mMessageQueueBlocked);
  mMessageQueueBlocked = false;
  ProcessControlMessageQueue();
}

void ImageDecoder::ProcessControlMessageQueue() {
  while (!mMessageQueueBlocked && !mControlMessageQueue.empty()) {
    auto& msg = mControlMessageQueue.front();
    auto result = MessageProcessedResult::Processed;
    if (auto* submsg = msg->AsConfigureMessage()) {
      result = ProcessConfigureMessage(submsg);
    } else if (auto* submsg = msg->AsDecodeMetadataMessage()) {
      result = ProcessDecodeMetadataMessage(submsg);
    } else if (auto* submsg = msg->AsDecodeFrameMessage()) {
      result = ProcessDecodeFrameMessage(submsg);
    } else if (auto* submsg = msg->AsSelectTrackMessage()) {
      result = ProcessSelectTrackMessage(submsg);
    } else {
      MOZ_ASSERT_UNREACHABLE("Unhandled control message type!");
    }

    if (result == MessageProcessedResult::NotProcessed) {
      break;
    }

    mControlMessageQueue.pop();
  }
}

MessageProcessedResult ImageDecoder::ProcessConfigureMessage(
    ConfigureMessage* aMsg) {
  // 10.2.2. Running a control message to configure the image decoder means
  // running these steps:

  // 1. Let supported be the result of running the Check Type Support algorithm
  // with init.type.
  //
  // 2. If supported is false, run the Close ImageDecoder algorithm with a
  // NotSupportedError DOMException and return "processed".
  NS_ConvertUTF16toUTF8 mimeType(mType);
  image::DecoderType type = image::ImageUtils::GetDecoderType(mimeType);
  if (NS_WARN_IF(type == image::DecoderType::UNKNOWN)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Initialize -- unsupported mime type '%s'", this,
             mimeType.get()));
    Close(MediaResult(NS_ERROR_DOM_NOT_SUPPORTED_ERR,
                      "Unsupported mime type"_ns));
    return MessageProcessedResult::Processed;
  }

  image::SurfaceFlags surfaceFlags = image::DefaultSurfaceFlags();
  switch (aMsg->mColorSpaceConversion) {
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
      Close(MediaResult(NS_ERROR_DOM_NOT_SUPPORTED_ERR,
                        "Unsupported colorspace conversion"_ns));
      return MessageProcessedResult::Processed;
  }

  // 3. Otherwise, assign the [[codec implementation]] internal slot with an
  // implementation supporting init.type
  mDecoder =
      image::ImageUtils::CreateDecoder(mSourceBuffer, type, surfaceFlags);
  if (NS_WARN_IF(!mDecoder)) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Error,
            ("ImageDecoder %p Initialize -- failed to create platform decoder",
             this));
    Close(MediaResult(NS_ERROR_DOM_NOT_SUPPORTED_ERR,
                      "Failed to create platform decoder"_ns));
    return MessageProcessedResult::Processed;
  }

  // 4. Assign true to [[message queue blocked]].
  mMessageQueueBlocked = true;

  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      "ImageDecoder::ProcessConfigureMessage", [self = RefPtr{this}] {
        // 5. Enqueue the following steps to the [[codec work queue]]:
        // 5.1. Configure [[codec implementation]] in accordance with the values
        //      given for colorSpaceConversion, desiredWidth, and desiredHeight.
        // 5.2. Assign false to [[message queue blocked]].
        // 5.3. Queue a task to Process the control message queue.
        self->ResumeControlMessageQueue();
      }));

  // 6. Return "processed".
  return MessageProcessedResult::Processed;
}

MessageProcessedResult ImageDecoder::ProcessDecodeMetadataMessage(
    DecodeMetadataMessage* aMsg) {
  // 10.2.2. Running a control message to decode track metadata means running
  // these steps:

  if (!mDecoder) {
    return MessageProcessedResult::Processed;
  }

  // 1. Enqueue the following steps to the [[codec work queue]]:
  // 1.1. Run the Establish Tracks algorithm.
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
  return MessageProcessedResult::Processed;
}

MessageProcessedResult ImageDecoder::ProcessDecodeFrameMessage(
    DecodeFrameMessage* aMsg) {
  // 10.4.2. Running a control message to decode the image means running these
  // steps:
  //
  // 1. Enqueue the following steps to the [[codec work queue]]:
  // 1.1. Wait for [[tracks established]] to become true.
  //
  // 1.2. If options.completeFramesOnly is false and the image is a
  //      Progressive Image for which the User Agent supports progressive
  //      decoding, run the Decode Progressive Frame algorithm with
  //      options.frameIndex and promise.
  //
  // 1.3. Otherwise, run the Decode Complete Frame algorithm with
  //      options.frameIndex and promise.
  return MessageProcessedResult::Processed;
}

MessageProcessedResult ImageDecoder::ProcessSelectTrackMessage(
    SelectTrackMessage* aMsg) {
  // 10.7.2. Running a control message to update the internal selected track
  // index means running these steps:
  //
  // 1. Enqueue the following steps to [[ImageDecoder]]'s [[codec work queue]]:
  // 1.1. Assign selectedIndex to [[internal selected track index]].
  // 1.2. Remove all entries from [[progressive frame generations]].
  //
  // At this time, progressive images and multi-track images are not supported.
  return MessageProcessedResult::Processed;
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

void ImageDecoder::OnSourceBufferComplete(const MediaResult& aResult) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageDecoder %p OnSourceBufferComplete -- success %d", this,
           NS_SUCCEEDED(aResult.Code())));

  MOZ_ASSERT(mSourceBuffer->IsComplete());

  if (NS_WARN_IF(NS_FAILED(aResult.Code()))) {
    OnCompleteFailed(aResult);
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

void ImageDecoder::OnCompleteFailed(const MediaResult& aResult) {
  if (mComplete) {
    return;
  }

  MOZ_LOG(gWebCodecsLog, LogLevel::Error,
          ("ImageDecoder %p OnCompleteFailed -- complete", this));
  mComplete = true;
  aResult.RejectTo(mCompletePromise);
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
          ("ImageDecoder %p OnMetadataFailed 0x%08x", this, aErr));

  MediaResult result(NS_ERROR_DOM_ENCODING_NOT_SUPPORTED_ERR,
                     "Metadata decoding failed"_ns);

  if (mTracks) {
    mTracks->OnMetadataFailed(aErr);
  }

  Close(result);
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

  if (mHasFramePending) {
    return;
  }

  mHasFramePending = true;

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
  Close(MediaResult(NS_ERROR_DOM_ENCODING_NOT_SUPPORTED_ERR,
                    "Frame count decoding failed"_ns));
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
  // 10.2.5. Decode Complete Frame (with frameIndex and promise)
  MOZ_ASSERT(mHasFramePending);

  // 1. Assert that [[tracks established]] is true.
  MOZ_ASSERT(mTracksEstablished);

  // Assert that [[internal selected track index]] is not -1.
  if (!mTracks) {
    return;
  }

  ImageTrack* track = mTracks->GetDefaultTrack();

  mHasFramePending = false;

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
  MOZ_ASSERT(mHasFramePending);
  mHasFramePending = false;

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

void ImageDecoder::Reset(const MediaResult& aResult) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("ImageDecoder %p Reset", this));
  // 10.2.5. Reset ImageDecoder (with exception)

  // 1. Signal [[codec implementation]] to abort any active decoding operation.
  if (mDecoder) {
    mDecoder->CancelDecodeFrames();
  }

  // FIXME could this be racy where a pending decode fullfillment is on the thread queue?
  mHasFramePending = false;

  // 2. For each decodePromise in [[pending decode promises]]:
  // 2.1. Reject decodePromise with exception.
  // 2.3. Remove decodePromise from [[pending decode promises]].
  AutoTArray<OutstandingDecode, 1> rejected = std::move(mOutstandingDecodes);
  for (const auto& i : rejected) {
    MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
            ("ImageDecoder %p Reset -- reject index %u", this, i.mFrameIndex));
    aResult.RejectTo(i.mPromise);
  }
}

void ImageDecoder::Close(const MediaResult& aResult) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("ImageDecoder %p Close", this));

  // 10.2.5. Algorithms - Close ImageDecoder (with exception)

  // 1. Run the Reset ImageDecoder algorithm with exception.
  Reset(aResult);

  if (mDecoder) {
    mDecoder->Destroy();
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
    aResult.RejectTo(mCompletePromise);
    mComplete = true;
  }
}

void ImageDecoder::Reset() {
  Reset(MediaResult(NS_ERROR_DOM_ABORT_ERR, "Reset decoder"_ns));
}

void ImageDecoder::Close() {
  Close(MediaResult(NS_ERROR_DOM_ABORT_ERR, "Closed decoder"_ns));
}

}  // namespace mozilla::dom
