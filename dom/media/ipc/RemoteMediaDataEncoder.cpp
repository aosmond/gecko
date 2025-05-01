/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoder.h"

#include "RemoteEncoderChild.h"
#include "RemoteMediaManagerChild.h"

namespace mozilla {

#define RMDE_LOG(str, ...)

RemoteMediaDataEncoder::RemoteMediaDataEncoder(RemoteEncoderChild* aChild)
    : mChild(aChild),
      mDescription("RemoteMediaDataEncoder"_ns),
      mIsHardwareAccelerated(false) {
  RMDE_LOG("%p is created", this);
}

RemoteMediaDataEncoder::~RemoteMediaDataEncoder() {
  if (mChild) {
    // Shutdown didn't get called. This can happen if the creation of the
    // decoder got interrupted while pending.
    nsCOMPtr<nsISerialEventTarget> thread =
        RemoteMediaManagerChild::GetManagerThread();
    MOZ_ASSERT(thread);
    thread->Dispatch(NS_NewRunnableFunction(
        "RemoteMediaDataEncoderShutdown", [child = std::move(mChild), thread] {
          child->Shutdown()->Then(
              thread, __func__,
              [child](const ShutdownPromise::ResolveOrRejectValue& aValue) {
                child->DestroyIPDL();
              });
        }));
  }
  RMDE_LOG("%p is released", this);
}

RefPtr<MediaDataEncoder::InitPromise> RemoteMediaDataEncoder::Init() {
  RefPtr<RemoteMediaDataEncoder> self = this;
  return InvokeAsync(RemoteMediaManagerChild::GetManagerThread(), __func__,
                     [self]() { return self->mChild->Init(); })
      ->Then(
          RemoteMediaManagerChild::GetManagerThread(), __func__,
          [self, this](TrackType aTrack) {
            MutexAutoLock lock(mMutex);
            // If shutdown has started in the meantime shutdown promise may
            // be resloved before this task. In this case mChild will be null
            // and the init promise has to be canceled.
            if (!mChild) {
              return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                                  __func__);
            }
            mDescription = mChild->GetDescriptionName();
            mIsHardwareAccelerated =
                mChild->IsHardwareAccelerated(mHardwareAcceleratedReason);
            RMDE_LOG(
                "%p RemoteEncoderChild has been initialized - description: %s",
                this, mDescription.get());
            return InitPromise::CreateAndResolve(aTrack, __func__);
          },
          [self](const MediaResult& aError) {
            return InitPromise::CreateAndReject(aError, __func__);
          });
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoder::Encode(
    const MediaData* aSample) {
  RefPtr<RemoteMediaDataEncoder> self = this;
  RefPtr<MediaData> sample = aSample;
  return InvokeAsync(
      RemoteMediaManagerChild::GetManagerThread(), __func__, [self, sample]() {
        return self->mChild->Encode(sample);
      });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
RemoteMediaDataEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  RefPtr<RemoteMediaDataEncoder> self = this;
  RefPtr<MediaData> configChanges = aConfigurationChanges;
  return InvokeAsync(RemoteMediaManagerChild::GetManagerThread(), __func__,
                     [self, configChanges]() {
                       return self->mChild->Reconfigure(configChanges);
                     });
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoder::Drain() {
  RefPtr<RemoteMediaDataEncoder> self = this;
  return InvokeAsync(RemoteMediaManagerChild::GetManagerThread(), __func__,
                     [self]() { return self->mChild->Drain(); });
}

RefPtr<ShutdownPromise> RemoteMediaDataEncoder::Shutdown() {
  RefPtr<RemoteMediaDataEncoder> self = this;
  return InvokeAsync(
      RemoteMediaManagerChild::GetManagerThread(), __func__, [self]() {
        RefPtr<ShutdownPromise> p = self->mChild->Shutdown();

        // We're about to be destroyed and drop our ref to
        // *EncoderChild. Make sure we put a ref into the
        // task queue for the *EncoderChild thread to keep
        // it alive until we send the delete message.
        p->Then(RemoteMediaManagerChild::GetManagerThread(), __func__,
                [child = std::move(self->mChild)](
                    const ShutdownPromise::ResolveOrRejectValue& aValue) {
                  MOZ_ASSERT(aValue.IsResolve());
                  child->DestroyIPDL();
                  return ShutdownPromise::CreateAndResolveOrReject(aValue,
                                                                   __func__);
                });
        return p;
      });
}

RefPtr<GenericPromise> RemoteMediaDataEncoder::SetBitrate(
    uint32_t aBitsPerSec) {
  RefPtr<RemoteMediaDataEncoder> self = this;
  return InvokeAsync(
      RemoteMediaManagerChild::GetManagerThread(), __func__,
      [self, aBitsPerSec]() { return self->mChild->SetBitrate(aBitsPerSec); });
}

bool RemoteMediaDataEncoder::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  MutexAutoLock lock(mMutex);
  aFailureReason = mHardwareAcceleratedReason;
  return mIsHardwareAccelerated;
}

nsCString RemoteMediaDataEncoder::GetDescriptionName() const {
  MutexAutoLock lock(mMutex);
  return mDescription;
}

}  // namespace mozilla
