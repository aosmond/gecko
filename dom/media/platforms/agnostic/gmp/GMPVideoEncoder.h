/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_GMPVideoEncoder_h_
#define mozilla_GMPVideoEncoder_h_

#include "PlatformEncoderModule.h"
#include "TimeUnits.h"

namespace mozilla {

class GMPVideoEncoder final : public MediaDataEncoder {
 public:
  using Config = H264Config;

  GMPVideoEncoder(const Config& aConfig, RefPtr<TaskQueue> aTaskQueue)
      : mConfig(aConfig),
        mTaskQueue(aTaskQueue),
        mFramesCompleted(false),
        mError(NS_OK) {
    MOZ_ASSERT(mConfig.mSize.width > 0 && mConfig.mSize.height > 0);
    MOZ_ASSERT(mTaskQueue);
  }

  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(Rate aBitsPerSec) override;

  nsCString GetDescriptionName() const override { return "gmp video encoder"_ns; }

 private:
  virtual ~GMPVideoEncoder() = default;
  RefPtr<EncodePromise> ProcessEncode(RefPtr<const VideoData> aSample);
  void ProcessOutput(RefPtr<MediaRawData>&& aOutput);
  void ResolvePromise();
  RefPtr<EncodePromise> ProcessDrain();
  RefPtr<ShutdownPromise> ProcessShutdown();

  void AssertOnTaskQueue() { MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn()); }

  const Config mConfig;
  const RefPtr<TaskQueue> mTaskQueue;
  // Access only in mTaskQueue.
  EncodedData mEncodedData;
  bool mFramesCompleted;
  RefPtr<MediaByteBuffer> mAvcc;  // Stores latest avcC data.
  MediaResult mError;
};

}  // namespace mozilla

#endif  // mozilla_GMPVideoEncoder_h_
