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
  MOZ_ASSERT(IsOnGMPThread());

  mMPS = do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  MOZ_ASSERT(mMPS);

  RefPtr<InitPromise> promise(mInitPromise.Ensure(__func__));

  nsTArray<nsCString> tags(1);
  tags.AppendElement("h264"_ns);

  UniquePtr<GetGMPVideoEncoderCallback> callback(new InitDoneCallback(this));
  if (NS_FAILED(mMPS->GetGMPVideoEncoder(nullptr, &tags, ""_ns,
                                         std::move(callback)))) {
    mInitPromise.Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
  }

  return promise;
}

void GMPVideoEncoder::InitComplete(GMPVideoEncoderProxy* aGMP,
                                   GMPVideoHost* aHost) {
  MOZ_ASSERT(IsOnGMPThread());

  if (NS_WARN_IF(!aGMP || !aHost)) {
    if (aGMP) {
      aGMP->Close();
    }
    mInitPromise.Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    return;
  }

  mGMP = aGMP;
  mHost = aHost;
  mInitPromise.Resolve(TrackInfo::TrackType::kVideoTrack, __func__);
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
  MOZ_ASSERT(IsOnGMPThread());

  if (NS_WARN_IF(!IsInitialized())) {
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  GMPVideoFrame* ftmp = nullptr;
  GMPErr err = mHost->CreateFrame(kGMPI420VideoFrame, &ftmp);
  if (NS_WARN_IF(err != GMPNoErr)) {
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  const VideoData* sample(aSample->As<const VideoData>());

  GMPUniquePtr<GMPVideoi420Frame> frame(static_cast<GMPVideoi420Frame*>(ftmp));
  err = frame->CreateFrame(

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
  return InvokeAsync(mTaskQueue, this, __func__,
                     &GMPVideoEncoder::ProcessDrain);
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

void GMPVideoEncoder::Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                              const nsTArray<uint8_t>& aCodecSpecificInfo) {}

void GMPVideoEncoder::Error(GMPErr aError) {}

void GMPVideoEncoder::Terminated() {}

}  // namespace mozilla
