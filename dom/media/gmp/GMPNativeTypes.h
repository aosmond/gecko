/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPNativeTypes_h_
#define GMPNativeTypes_h_

#include "mozilla/TypedEnumBits.h"

enum class GMPPluginType {
  Unknown,
  Fake,
  Clearkey,
  OpenH264,
  Widevine,
  WidevineL1,
};

enum class GMPCapabilityFlags : uint32_t {
  None = 0,
  DecodeH264Level5 = 1 << 0,
  EncodeH264Level5 = 1 << 1,
  EncodeH264SVC = 1 << 2,
  ALL_BITS = (1 << 3) - 1
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(GMPCapabilityFlags)

#endif
