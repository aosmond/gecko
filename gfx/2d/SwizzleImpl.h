/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLE_IMPL_H_
#define MOZILLA_GFX_SWIZZLE_IMPL_H_

#include <stdint.h>

namespace mozilla {
namespace gfx {

template <bool aSwapRB>
void UnpackRowRGB24(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  // Because we are expanding, we can only process the data back to front in
  // case we are performing this in place.
  const uint8_t* src = aSrc + 3 * (aLength - 1);
  uint8_t* dst = aDst + 4 * (aLength - 1);
  while (src >= aSrc) {
    uint8_t r = src[aSwapRB ? 2 : 0];
    uint8_t g = src[1];
    uint8_t b = src[aSwapRB ? 0 : 2];

    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = 0xFF;

    src -= 3;
    dst -= 4;
  }
}

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SWIZZLE_H_ */
