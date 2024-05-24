/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoEncoder.h"

#include "H264.h"
#include "GMPLog.h"
#include "GMPUtils.h"
#include "GMPService.h"
#include "GMPVideoHost.h"
#include "ImageContainer.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsServiceManagerUtils.h"
#include "prsystem.h"

namespace mozilla {

static GMPVideoCodecMode ToGMPVideoCodecMode(Usage aUsage) {
  switch (aUsage) {
    case Usage::Realtime:
      return kGMPRealtimeVideo;
    case Usage::Record:
    default:
      return kGMPNonRealtimeVideo;
  }
}

static GMPProfile ToGMPProfile(H264_PROFILE aProfile) {
  switch (aProfile) {
    case H264_PROFILE_MAIN:
      return kGMPH264ProfileMain;
    case H264_PROFILE_EXTENDED:
      return kGMPH264ProfileExtended;
    case H264_PROFILE_HIGH:
      return kGMPH264ProfileHigh;
    case H264_PROFILE_UNKNOWN:
    default:
      return kGMPH264ProfileUnknown;
  }
}

static GMPLevel ToGMPLevel(H264_LEVEL aLevel) {
  switch (aLevel) {
    case H264_LEVEL_1:
      return kGMPH264Level1_0;
    // case H264_LEVEL_1_b:
    //   return kGMPH264Level1_B;
    case H264_LEVEL_1_1:
      return kGMPH264Level1_1;
    case H264_LEVEL_1_2:
      return kGMPH264Level1_2;
    case H264_LEVEL_1_3:
      return kGMPH264Level1_3;
    case H264_LEVEL_2:
      return kGMPH264Level2_0;
    case H264_LEVEL_2_1:
      return kGMPH264Level2_1;
    case H264_LEVEL_2_2:
      return kGMPH264Level2_2;
    case H264_LEVEL_3:
      return kGMPH264Level3_0;
    case H264_LEVEL_3_1:
      return kGMPH264Level3_1;
    case H264_LEVEL_3_2:
      return kGMPH264Level3_2;
    case H264_LEVEL_4:
      return kGMPH264Level4_0;
    case H264_LEVEL_4_1:
      return kGMPH264Level4_1;
    case H264_LEVEL_4_2:
      return kGMPH264Level4_2;
    case H264_LEVEL_5:
      return kGMPH264Level5_0;
    case H264_LEVEL_5_1:
      return kGMPH264Level5_1;
    case H264_LEVEL_5_2:
      return kGMPH264Level5_2;
    default:
      return kGMPH264LevelUnknown;
  }
}

RefPtr<MediaDataEncoder::InitPromise> GMPVideoEncoder::Init() {
  MOZ_ASSERT(IsOnGMPThread());
  MOZ_ASSERT(mInitPromise.IsEmpty());

  mMPS = do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  MOZ_ASSERT(mMPS);

  RefPtr<InitPromise> promise(mInitPromise.Ensure(__func__));

  nsTArray<nsCString> tags(1);
  tags.AppendElement("h264"_ns);

  UniquePtr<GetGMPVideoEncoderCallback> callback(new InitDoneCallback(this));
  if (NS_FAILED(mMPS->GetGMPVideoEncoder(nullptr, &tags, ""_ns,
                                         std::move(callback)))) {
    GMP_LOG_ERROR("[%p] GMPVideoEncoder::Init -- failed to request encoder",
                  this);
    mInitPromise.Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
  }

  return promise;
}

void GMPVideoEncoder::InitComplete(GMPVideoEncoderProxy* aGMP,
                                   GMPVideoHost* aHost) {
  MOZ_ASSERT(IsOnGMPThread());

  mGMP = aGMP;
  mHost = aHost;

  if (NS_WARN_IF(!mGMP) || NS_WARN_IF(!mHost) ||
      NS_WARN_IF(mInitPromise.IsEmpty())) {
    GMP_LOG_ERROR(
        "[%p] GMPVideoEncoder::InitComplete -- failed to create proxy/host",
        this);
    Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    return;
  }

  GMPVideoCodec codec{};

  codec.mGMPApiVersion = kGMPVersion35;
  codec.mCodecType = kGMPVideoCodecH264;
  codec.mMode = ToGMPVideoCodecMode(mConfig.mUsage);
  codec.mWidth = mConfig.mSize.width;
  codec.mHeight = mConfig.mSize.height;
  codec.mStartBitrate = mConfig.mBitrate / 1000;
  codec.mMinBitrate = mConfig.mMinBitrate / 1000;
  codec.mMaxBitrate = mConfig.mMaxBitrate ? mConfig.mMaxBitrate / 1000
                                          : codec.mStartBitrate * 2;
  codec.mMaxFramerate = mConfig.mFramerate;
  codec.mUseThreadedEncode = StaticPrefs::media_gmp_encoder_multithreaded();
  codec.mLogLevel = GetGMPLibraryLogLevel();

  if (mConfig.mCodecSpecific) {
    const H264Specific& specific = mConfig.mCodecSpecific->as<H264Specific>();
    codec.mProfile = ToGMPProfile(specific.mProfile);
    codec.mLevel = ToGMPLevel(specific.mLevel);
  }

  nsTArray<uint8_t> codecSpecific;
  GMPErr err = mGMP->InitEncode(codec, codecSpecific, this,
                                PR_GetNumberOfProcessors(), 0);
  if (NS_WARN_IF(err != GMPNoErr)) {
    GMP_LOG_ERROR("[%p] GMPVideoEncoder::InitComplete -- failed to init proxy",
                  this);
    Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    return;
  }

  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::InitComplete -- encoder initialized",
                this);
  mInitPromise.Resolve(TrackInfo::TrackType::kVideoTrack, __func__);
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
    GMP_LOG_ERROR("[%p] GMPVideoEncoder::Encode -- failed to create frame",
                  this);
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  const VideoData* sample(aSample->As<const VideoData>());
  const layers::PlanarYCbCrImage* image = sample->mImage->AsPlanarYCbCrImage();
  const layers::PlanarYCbCrData* yuv = image->GetData();
  const gfx::IntSize ySize = yuv->YDataSize();
  const gfx::IntSize cbCrSize = yuv->CbCrDataSize();
  const int32_t yStride = yuv->mYStride;
  const int32_t cbCrStride = yuv->mCbCrStride;

  CheckedInt32 yBufSize = CheckedInt32(yStride) * ySize.height;
  MOZ_RELEASE_ASSERT(yBufSize.isValid());

  CheckedInt32 cbCrBufSize = CheckedInt32(cbCrStride) * cbCrSize.height;
  MOZ_RELEASE_ASSERT(cbCrBufSize.isValid());

  GMPUniquePtr<GMPVideoi420Frame> frame(static_cast<GMPVideoi420Frame*>(ftmp));
  err = frame->CreateFrame(yBufSize.value(), yuv->mYChannel,
                           cbCrBufSize.value(), yuv->mCbChannel,
                           cbCrBufSize.value(), yuv->mCrChannel, ySize.width,
                           ySize.height, yStride, cbCrStride, cbCrStride);
  if (NS_WARN_IF(err != GMPNoErr)) {
    GMP_LOG_ERROR(
        "[%p] GMPVideoEncoder::Encode -- failed to allocate frame data", this);
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  uint64_t timestamp = sample->mTime.ToMicroseconds();
  frame->SetTimestamp(timestamp);

  AutoTArray<GMPVideoFrameType, 1> frameType;
  frameType.AppendElement(sample->mKeyframe ? kGMPKeyFrame : kGMPDeltaFrame);

  nsTArray<uint8_t> codecSpecific;
  err = mGMP->Encode(std::move(frame), codecSpecific, frameType);
  if (NS_WARN_IF(err != GMPNoErr)) {
    GMP_LOG_ERROR("[%p] GMPVideoEncoder::Encode -- failed to queue frame",
                  this);
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  GMP_LOG_DEBUG(
      "[%p] GMPVideoEncoder::Encode -- request encode of frame @ %" PRIu64
      " y %dx%d stride=%u cbCr %dx%d stride=%u",
      this, timestamp, ySize.width, ySize.height, yStride, cbCrSize.width,
      cbCrSize.height, cbCrStride);

  RefPtr<EncodePromise::Private> promise = new EncodePromise::Private(__func__);
  mPendingEncodes.InsertOrUpdate(timestamp, promise);
  return promise.forget();
}

RefPtr<MediaDataEncoder::ReconfigurationPromise> GMPVideoEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  // General reconfiguration interface not implemented right now
  return MediaDataEncoder::ReconfigurationPromise::CreateAndReject(
      NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> GMPVideoEncoder::Drain() {
  MOZ_ASSERT(IsOnGMPThread());
  MOZ_ASSERT(mDrainPromise.IsEmpty());

  if (NS_WARN_IF(!IsInitialized())) {
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  if (mPendingEncodes.IsEmpty()) {
    return EncodePromise::CreateAndResolve(EncodedData(), __func__);
  }

  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::Drain -- waiting for queue to clear",
                this);
  return mDrainPromise.Ensure(__func__);
}

RefPtr<ShutdownPromise> GMPVideoEncoder::Shutdown() {
  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::Shutdown", this);
  MOZ_ASSERT(IsOnGMPThread());

  Teardown(NS_ERROR_ABORT, __func__);
  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> GMPVideoEncoder::SetBitrate(uint32_t aBitsPerSec) {
  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::SetBitrate -- %u", this, aBitsPerSec);
  MOZ_ASSERT(IsOnGMPThread());

  if (NS_WARN_IF(!IsInitialized())) {
    return GenericPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                           __func__);
  }

  GMPErr err = mGMP->SetRates(aBitsPerSec / 1000, mConfig.mFramerate);
  if (NS_WARN_IF(err != GMPNoErr)) {
    return GenericPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                           __func__);
  }

  return GenericPromise::CreateAndResolve(true, __func__);
}

void GMPVideoEncoder::Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                              const nsTArray<uint8_t>& aCodecSpecificInfo) {
  MOZ_ASSERT(IsOnGMPThread());
  MOZ_ASSERT(aEncodedFrame);

  uint64_t timestamp = aEncodedFrame->TimeStamp();

  RefPtr<EncodePromise::Private> promise;
  if (!mPendingEncodes.Remove(timestamp, getter_AddRefs(promise))) {
    GMP_LOG_WARNING(
        "[%p] GMPVideoEncoder::Encoded -- no frame matching timestamp %" PRIu64,
        this, timestamp);
    return;
  }

  uint8_t* encodedData = aEncodedFrame->Buffer();
  uint32_t encodedSize = aEncodedFrame->Size();

  if (NS_WARN_IF(encodedSize == 0) || NS_WARN_IF(!encodedData) ||
      NS_WARN_IF(aEncodedFrame->BufferType() != GMP_BufferLength32)) {
    GMP_LOG_ERROR("[%p] GMPVideoEncoder::Encoded -- bad/empty frame", this);
    promise->Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    return;
  }

  // This code was copied from WebrtcGmpVideoEncoder::Encoded in order to
  // massage/correct issues with OpenH264 and WebRTC. This allows us to use the
  // PlatformEncoderModule framework with WebRTC, fallback to this encoder and
  // actually render the video.

  // Libwebrtc's RtpPacketizerH264 expects a 3- or 4-byte NALU start sequence
  // before the start of the NALU payload. {0,0,1} or {0,0,0,1}. We set this
  // in-place. Any other length of the length field we reject.

  const int sizeNumBytes = 4;
  uint32_t unitOffset = 0;
  uint32_t unitSize = 0;
  // Make sure we don't read past the end of the buffer getting the size
  while (unitOffset + sizeNumBytes < encodedSize) {
    uint8_t* unitBuffer = encodedData + unitOffset;
#if MOZ_LITTLE_ENDIAN()
    unitSize = LittleEndian::readUint32(unitBuffer);
#else
    unitSize = BigEndian::readUint32(unitBuffer);
#endif
    const uint8_t startSequence[] = {0, 0, 0, 1};
    if (memcmp(unitBuffer, startSequence, 4) == 0) {
      // This is a bug in OpenH264 where it misses to convert the NALU start
      // sequence to the NALU size per the GMP protocol. We mitigate this by
      // letting it through as this is what libwebrtc already expects and
      // scans for.
      unitSize = encodedSize - 4;
    } else {
      memcpy(unitBuffer, startSequence, 4);
    }

    MOZ_ASSERT(unitSize != 0);
    MOZ_ASSERT(unitOffset + sizeNumBytes + unitSize <= encodedSize);
    if (unitSize == 0 || unitOffset + sizeNumBytes + unitSize > encodedSize) {
      GMP_LOG_ERROR(
          "[%p] GMPVideoEncoder::Encoded -- bad frame data; unitOffset=%u, "
          "unitSize=%u, size=%u",
          this, unitOffset, unitSize, encodedSize);
      promise->Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
      Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
      return;
    }

    unitOffset += sizeNumBytes + unitSize;
  }

  auto output = MakeRefPtr<MediaRawData>();

  UniquePtr<MediaRawDataWriter> writer(output->CreateWriter());
  if (NS_WARN_IF(!writer->SetSize(encodedSize))) {
    GMP_LOG_ERROR(
        "[%p] GMPVideoEncoder::Encoded -- failed to allocate %u buffer", this,
        encodedSize);
    promise->Reject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    return;
  }

  memcpy(writer->Data(), encodedData, encodedSize);

  output->mTime =
      media::TimeUnit::FromMicroseconds(static_cast<int64_t>(timestamp));
  output->mKeyframe = aEncodedFrame->FrameType() == kGMPKeyFrame;

  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::Encoded -- %sframe @ timestamp %" PRIu64,
                this, output->mKeyframe ? "key" : "", timestamp);

  EncodedData encodedDataSet(1);
  encodedDataSet.AppendElement(std::move(output));
  promise->Resolve(std::move(encodedDataSet), __func__);

  if (mPendingEncodes.IsEmpty()) {
    mDrainPromise.ResolveIfExists(EncodedData(), __func__);
  }
}

void GMPVideoEncoder::Teardown(nsresult aRejectReason, StaticString aCallSite) {
  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::Teardown", this);
  MOZ_ASSERT(IsOnGMPThread());

  PendingEncodePromises pendingEncodes = std::move(mPendingEncodes);
  for (auto i = pendingEncodes.Iter(); !i.Done(); i.Next()) {
    i.Data()->Reject(aRejectReason, aCallSite);
  }

  mInitPromise.RejectIfExists(aRejectReason, aCallSite);
  mDrainPromise.RejectIfExists(aRejectReason, aCallSite);

  if (mGMP) {
    mGMP->Close();
    mGMP = nullptr;
  }

  mHost = nullptr;
}

void GMPVideoEncoder::Error(GMPErr aError) {
  GMP_LOG_ERROR("[%p] GMPVideoEncoder::Error -- GMPErr(%u)", this,
                uint32_t(aError));
  MOZ_ASSERT(IsOnGMPThread());
  Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
}

void GMPVideoEncoder::Terminated() {
  GMP_LOG_DEBUG("[%p] GMPVideoEncoder::Terminated", this);
  MOZ_ASSERT(IsOnGMPThread());
  Teardown(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
}

}  // namespace mozilla
