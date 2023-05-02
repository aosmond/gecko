/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoEncoder.h"

#include "H264.h"
#include "GMPUtils.h"

namespace mozilla {
extern LazyLogModule sPEMLog;
#define VTENC_LOGE(fmt, ...)                 \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Error, \
          ("[GMPVideoEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define VTENC_LOGD(fmt, ...)                 \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Debug, \
          ("[GMPVideoEncoder] %s: " fmt, __func__, ##__VA_ARGS__))

RefPtr<MediaDataEncoder::InitPromise> GMPVideoEncoder::Init() {
  if (mConfig.mSize.width == 0 || mConfig.mSize.height == 0) {
    return InitPromise::CreateAndReject(NS_ERROR_ILLEGAL_VALUE, __func__);
  }

  return InitPromise::CreateAndResolve(TrackInfo::TrackType::kVideoTrack,
                                       __func__);
}

void GMPVideoEncoder::ProcessOutput(RefPtr<MediaRawData>&& aOutput) {
  if (!mTaskQueue->IsCurrentThreadIn()) {
    nsresult rv = mTaskQueue->Dispatch(NewRunnableMethod<RefPtr<MediaRawData>>(
        "GMPVideoEncoder::ProcessOutput", this, &GMPVideoEncoder::ProcessOutput,
        std::move(aOutput)));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
    return;
  }
  AssertOnTaskQueue();

  if (aOutput) {
    mEncodedData.AppendElement(std::move(aOutput));
  } else {
    mError = NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }
}

RefPtr<MediaDataEncoder::EncodePromise> GMPVideoEncoder::Encode(
    const MediaData* aSample) {
  MOZ_ASSERT(aSample != nullptr);
  RefPtr<const VideoData> sample(aSample->As<const VideoData>());

  return InvokeAsync<RefPtr<const VideoData>>(mTaskQueue, this, __func__,
                                              &GMPVideoEncoder::ProcessEncode,
                                              std::move(sample));
}

RefPtr<MediaDataEncoder::EncodePromise> GMPVideoEncoder::ProcessEncode(
    RefPtr<const VideoData> aSample) {
  AssertOnTaskQueue();

  if (NS_FAILED(mError)) {
    return EncodePromise::CreateAndReject(mError, __func__);
  }

  return EncodePromise::CreateAndResolve(std::move(mEncodedData), __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> GMPVideoEncoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__, &GMPVideoEncoder::ProcessDrain);
}

RefPtr<MediaDataEncoder::EncodePromise> GMPVideoEncoder::ProcessDrain() {
  AssertOnTaskQueue();

  if (mFramesCompleted) {
    MOZ_DIAGNOSTIC_ASSERT(mEncodedData.IsEmpty());
    return EncodePromise::CreateAndResolve(EncodedData(), __func__);
  }

  mFramesCompleted = true;
  // VTCompressionSessionCompleteFrames() could have queued multiple tasks with
  // the new drained frames. Dispatch a task after them to resolve the promise
  // with those frames.
  RefPtr<GMPVideoEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    EncodedData pendingFrames(std::move(self->mEncodedData));
    self->mEncodedData = EncodedData();
    return EncodePromise::CreateAndResolve(std::move(pendingFrames), __func__);
  });
}

RefPtr<ShutdownPromise> GMPVideoEncoder::Shutdown() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &GMPVideoEncoder::ProcessShutdown);
}

RefPtr<ShutdownPromise> GMPVideoEncoder::ProcessShutdown() {
  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> GMPVideoEncoder::SetBitrate(
    MediaDataEncoder::Rate aBitsPerSec) {
  RefPtr<GMPVideoEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, aBitsPerSec]() {
      return GenericPromise::CreateAndResolve(true, __func__);
  });
}

}  // namespace mozilla
