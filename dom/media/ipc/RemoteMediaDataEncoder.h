/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef include_dom_media_ipc_RemoteMediaDataEncoder_h_
#define include_dom_media_ipc_RemoteMediaDataEncoder_h_

#include "PlatformEncoderModule.h"

namespace mozilla {

class RemoteEncoderChild;

class RemoteMediaDataEncoder final : public MediaDataEncoder {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteMediaDataEncoder, final);

  explicit RemoteMediaDataEncoder(RemoteEncoderChild* aChild);

  // MediaDataEncoder
  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override;
  nsCString GetDescriptionName() const;

 protected:
  ~RemoteMediaDataEncoder() override;

  // Only ever written to from the reader task queue (during the constructor and
  // destructor when we can guarantee no other threads are accessing it). Only
  // read from the manager thread.
  RefPtr<RemoteEncoderChild> mChild;

  mutable Mutex mMutex{"RemoteMediaDataEncoder"};

  // Only ever written/modified during decoder initialisation.
  nsCString mDescription MOZ_GUARDED_BY(mMutex);
  bool mIsHardwareAccelerated MOZ_GUARDED_BY(mMutex);
  nsCString mHardwareAcceleratedReason MOZ_GUARDED_BY(mMutex);
};

}  // namespace mozilla

#endif /* include_dom_media_ipc_RemoteMediaDataEncoder_h_ */
