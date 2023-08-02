/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPNativeTypes_h_
#define GMPNativeTypes_h_

#include "base/process.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsString.h"

namespace mozilla::gmp {
class PGMPContentParent;
}  // namespace mozilla::gmp

enum class GMPPluginType {
  Unknown,
  Fake,
  Clearkey,
  OpenH264,
  Widevine,
};

struct GMPLaunchResult {
  GMPLaunchResult(const GMPLaunchResult&) = delete;
  GMPLaunchResult& operator=(const GMPLaunchResult&) = delete;

  GMPLaunchResult() = default;
  GMPLaunchResult(GMPLaunchResult&& aOther) = default;
  GMPLaunchResult& operator=(GMPLaunchResult&& aOther) = default;

  uint32_t mPluginId = 0;
  GMPPluginType mPluginType = GMPPluginType::Unknown;
  base::ProcessId mPid = base::kInvalidProcessId;
  nsCString mDisplayName;
  mozilla::ipc::Endpoint<mozilla::gmp::PGMPContentParent> mEndpoint;
  nsresult mResult = NS_ERROR_FAILURE;
  nsCString mErrorDescription;
};

#endif
