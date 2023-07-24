/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageDecoder.h"
#include <algorithm>
#include <cstdint>
#include "ImageContainer.h"
#include "nsComponentManagerUtils.h"
#include "nsTHashSet.h"
#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/image/ImageUtils.h"
#include "mozilla/image/SourceBuffer.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageDecoder, mParent, mTracks,
                                      mCompletePromise)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageDecoder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageDecoder)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageDecoder)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageDecoder::ImageDecoder(nsCOMPtr<nsIGlobalObject>&& aParent,
                           const nsAString& aType)
    : mParent(std::move(aParent)), mType(aType) {}

ImageDecoder::~ImageDecoder() = default;

JSObject* ImageDecoder::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageDecoder_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */ already_AddRefed<ImageDecoder> ImageDecoder::Constructor(
    const GlobalObject& aGlobal, const ImageDecoderInit& aInit,
    ErrorResult& aRv) {
  // 10.2.2.1 If init is not valid ImageDecoderInit, throw a TypeError.
  // 10.3.1 If type is not a valid image MIME type, return false.
  const auto mimeType = Substring(aInit.mType, 6);
  if (!mimeType.Equals(u"image/"_ns)) {
    aRv.ThrowTypeError("Invalid MIME type, must be 'image'");
    return nullptr;
  }

  const uint8_t* dataContents = nullptr;
  size_t dataSize = 0;

  if (aInit.mData.IsReadableStream()) {
    const auto& stream = aInit.mData.GetAsReadableStream();
    // 10.3.2 If data is of type ReadableStream and the ReadableStream is
    // disturbed or locked, return false.
    if (stream->Disturbed() || stream->Locked()) {
      aRv.ThrowTypeError("ReadableStream data is disturbed and/or locked");
      return nullptr;
    }
  } else {
    if (aInit.mData.IsArrayBufferView()) {
      const auto& view = aInit.mData.GetAsArrayBufferView();
      view.ComputeState();
      dataContents = view.Data();
      dataSize = view.Length();
    } else if (aInit.mData.IsArrayBuffer()) {
      const auto& buffer = aInit.mData.GetAsArrayBuffer();
      buffer.ComputeState();
      dataContents = buffer.Data();
      dataSize = buffer.Length();
    } else {
      MOZ_ASSERT_UNREACHABLE("Unsupported data type!");
      aRv.ThrowNotSupportedError("Unsupported data type");
      return nullptr;
    }

    // 10.3.3.1 If the result of running IsDetachedBuffer (described in
    // [ECMASCRIPT]) on data is false, return false.
    if (!dataContents) {
      aRv.ThrowTypeError("BufferSource is detached");
      return nullptr;
    }
    // 10.3.3.2 If data is empty, return false.
    if (dataSize == 0) {
      aRv.ThrowTypeError("BufferSource is empty");
      return nullptr;
    }
  }

  // 10.3.4 If desiredWidth exists and desiredHeight does not exist, return
  // false. 10.3.5 If desiredHeight exists and desiredWidth does not exist,
  // return false.
  if (aInit.mDesiredHeight.WasPassed() != aInit.mDesiredWidth.WasPassed()) {
    aRv.ThrowTypeError(
        "Both or neither of desiredHeight and desiredWidth must be passed");
    return nullptr;
  }

  nsTHashSet<const uint8_t*> transferSet;
  for (const auto& buffer : aInit.mTransfer) {
    buffer.ComputeState();
    const uint8_t* transferDataContents = buffer.Data();
    // 10.2.2.3.1 If [[Detached]] internal slot is true, then throw a
    // DataCloneError DOMException.
    if (!transferDataContents) {
      aRv.ThrowDataCloneError("Transfer contains detached ArrayBuffer objects");
      return nullptr;
    }
    // 10.2.2.2 If init.transfer contains more than one reference to the same
    // ArrayBuffer, then throw a DataCloneError DOMException.
    if (transferSet.Contains(transferDataContents)) {
      aRv.ThrowDataCloneError(
          "Transfer contains duplicate ArrayBuffer objects");
      return nullptr;
    }
    transferSet.Insert(transferDataContents);
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  auto imageDecoder = MakeRefPtr<ImageDecoder>(std::move(global), aInit.mType);
  imageDecoder->Initialize(aInit, dataContents, dataSize, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
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

  const auto subType = Substring(aType, 6);
  if (!subType.Equals(u"image/"_ns)) {
    promise->MaybeRejectWithTypeError("Invalid MIME type, must be 'image'");
    return promise.forget();
  }

  NS_ConvertUTF16toUTF8 mimeType(aType);
  image::DecoderType type = image::ImageUtils::GetDecoderType(mimeType);
  promise->MaybeResolve(type != image::DecoderType::UNKNOWN);
  return promise.forget();
}

void ImageDecoder::Initialize(const ImageDecoderInit& aInit,
                              const uint8_t* aData, size_t aLength,
                              ErrorResult& aRv) {
  mCompletePromise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  mTracks = MakeAndAddRef<ImageTrackList>(mParent);
  mTracks->Initialize(aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  RefPtr<image::CreateBufferPromise> bufferPromise;
  if (aData) {
    MOZ_ASSERT(aLength > 0);
    bufferPromise = image::ImageUtils::CreateSourceBuffer(aData, aLength);
  } else {
    MOZ_ASSERT(aInit.mData.IsReadableStream());
    const auto& stream = aInit.mData.GetAsReadableStream();
    nsIInputStream* inputStream = stream->MaybeGetInputStreamIfUnread();
    bufferPromise = image::ImageUtils::CreateSourceBuffer(inputStream);
  }

  if (NS_WARN_IF(!bufferPromise)) {
    mComplete = true;
    mCompletePromise->MaybeRejectWithInvalidStateError(
        "Failed to create image encoded buffer");
    return;
  }

  NS_ConvertUTF16toUTF8 mimeType(mType);
  image::DecoderType type = image::ImageUtils::GetDecoderType(mimeType);

  // FIXME configuration from aInit
  image::SurfaceFlags surfaceFlags = image::DefaultSurfaceFlags();

  bufferPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, type,
       surfaceFlags](const RefPtr<image::SourceBuffer>& aSourceBuffer) {
        self->OnSourceBufferReady(aSourceBuffer, type, surfaceFlags);
      },
      [self = RefPtr{this}](const nsresult& aErr) {
        self->mComplete = true;
        self->mCompletePromise->MaybeRejectWithInvalidStateError(
            "Failed to buffer encoded data");
      });
}

void ImageDecoder::OnSourceBufferReady(image::SourceBuffer* aSourceBuffer,
                                       image::DecoderType aType,
                                       image::SurfaceFlags aSurfaceFlags) {
  mSourceBuffer = aSourceBuffer;
  mComplete = true;

  mDecoder =
      image::ImageUtils::CreateDecoder(aSourceBuffer, aType, aSurfaceFlags);
  if (NS_WARN_IF(!mDecoder)) {
    mCompletePromise->MaybeResolveWithUndefined();
    mTracks->OnMetadataFailed(NS_ERROR_FAILURE);
    return;
  }

  mDecoder->Initialize()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}](const image::DecodeMetadataResult& aMetadata) {
        self->mTracks->OnMetadataSuccess(aMetadata);
      },
      [self = RefPtr{this}](const nsresult& aErr) {
        self->mTracks->OnMetadataFailed(aErr);
      });

  mCompletePromise->MaybeResolveWithUndefined();
}

void ImageDecoder::GetType(nsAString& aType) const { aType.Assign(mType); }

already_AddRefed<Promise> ImageDecoder::Decode(
    const ImageDecodeOptions& aOptions, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (NS_WARN_IF(!mSourceBuffer)) {
    promise->MaybeRejectWithInvalidStateError("No buffered encoded data");
    return promise.forget();
  }

  if (NS_WARN_IF(!mDecoder)) {
    promise->MaybeRejectWithInvalidStateError("No buffered encoded data");
    return promise.forget();
  }

  if (aOptions.mFrameIndex < mDecodedFrames.Length()) {
    ImageDecodeResult result;
    result.mImage = mDecodedFrames[aOptions.mFrameIndex];
    result.mComplete = true;
    promise->MaybeResolve(result);
    return promise.forget();
  }

  if (mOutstandingDecodes.IsEmpty()) {
    mDecoder->DecodeFrames(aOptions.mFrameIndex + 1 - mDecodedFrames.Length())
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self = RefPtr{this}](const image::DecodeFramesResult& aResult) {
              self->OnDecodeFramesSuccess(aResult);
            },
            [self = RefPtr{this}](const nsresult& aErr) {
              self->OnDecodeFramesFailed(aErr);
            });
  }

  mOutstandingDecodes.AppendElement(
      OutstandingDecode{promise, aOptions.mFrameIndex});
  return promise.forget();
}

void ImageDecoder::OnDecodeFramesSuccess(
    const image::DecodeFramesResult& aResult) {
  mDecodedFrames.SetCapacity(mDecodedFrames.Length() +
                             aResult.mSurfaces.Length());
  for (const auto& surface : aResult.mSurfaces) {
    VideoColorSpaceInit colorSpace;
    gfx::IntSize size = surface->GetSize();
    gfx::IntRect rect(gfx::IntPoint(0, 0), size);
    auto image = MakeRefPtr<layers::SourceSurfaceImage>(size, surface);
    // FIXME animation duration, time
    auto frame =
        MakeRefPtr<VideoFrame>(mParent, image, Some(VideoPixelFormat::BGRA),
                               size, rect, size, Nothing(), 0, colorSpace);
    mDecodedFrames.AppendElement(std::move(frame));
  }

  uint32_t minFrameIndex = UINT32_MAX;
  AutoTArray<OutstandingDecode, 1> resolved;
  uint32_t i = mOutstandingDecodes.Length();
  while (i > 0) {
    --i;

    const auto frameIndex = mOutstandingDecodes[i].mFrameIndex;
    if (frameIndex < mDecodedFrames.Length()) {
      resolved.AppendElement(std::move(mOutstandingDecodes[i]));
      mOutstandingDecodes.RemoveElementAt(i);
    } else {
      minFrameIndex = std::min(minFrameIndex, frameIndex);
    }
  }

  if (!mOutstandingDecodes.IsEmpty()) {
    mDecoder->DecodeFrames(minFrameIndex + 1 - mDecodedFrames.Length())
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self = RefPtr{this}](const image::DecodeFramesResult& aResult) {
              self->OnDecodeFramesSuccess(aResult);
            },
            [self = RefPtr{this}](const nsresult& aErr) {
              self->OnDecodeFramesFailed(aErr);
            });
  }

  for (const auto& i : resolved) {
    ImageDecodeResult result;
    result.mImage = mDecodedFrames[i.mFrameIndex];
    result.mComplete = true;
    i.mPromise->MaybeResolve(result);
  }
}

void ImageDecoder::OnDecodeFramesFailed(const nsresult& aErr) {
  AutoTArray<OutstandingDecode, 1> resolved = std::move(mOutstandingDecodes);
  for (const auto& i : resolved) {
    i.mPromise->MaybeRejectWithInvalidStateError("Failed to decode frame");
  }
}

void ImageDecoder::Reset() {}

void ImageDecoder::Close() {}

}  // namespace mozilla::dom
