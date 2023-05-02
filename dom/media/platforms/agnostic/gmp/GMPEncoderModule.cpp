/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPEncoderModule.h"

#include "GMPService.h"
#include "GMPUtils.h"
#include "GMPVideoEncoder.h"
#include "MediaDataEncoderProxy.h"
#include "MP4Decoder.h"

namespace mozilla {

bool GMPEncoderModule::SupportsMimeType(const nsACString& aMimeType) const {
  if (!MP4Decoder::IsH264(aMimeType)) {
    return false;
  }

  return HaveGMPFor("encode-video"_ns, {"h264"_ns});
}

already_AddRefed<MediaDataEncoder> GMPEncoderModule::CreateVideoEncoder(
    const CreateEncoderParams& aParams) const {
  if (!MP4Decoder::IsH264(aParams.mConfig.mMimeType)) {
    return nullptr;
  }

  RefPtr<gmp::GeckoMediaPluginService> s(
      gmp::GeckoMediaPluginService::GetGeckoMediaPluginService());
  if (!s) {
    return nullptr;
  }

  nsCOMPtr<nsISerialEventTarget> thread(s->GetGMPThread());
  if (!thread) {
    return nullptr;
  }

  RefPtr<MediaDataEncoder> encoder(
      new GMPVideoEncoder(aParams.ToH264Config(), aParams.mTaskQueue));
  return do_AddRef(
      new MediaDataEncoderProxy(encoder.forget(), thread.forget()));
}

}  // namespace mozilla
