/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLEAVX2_H_
#define MOZILLA_GFX_SWIZZLEAVX2_H_

#include "SwizzleSSE2.h"

#include <immintrin.h>

namespace mozilla {
namespace gfx {

// Load 1-7 pixels into a 8 pixel vector.
static MOZ_ALWAYS_INLINE __m256i LoadRemainder_AVX2(const uint8_t* aSrc,
                                                    size_t aLength) {
  __m128i px_lo;

  if (aLength >= 4) {
    // Load first 4 pixels.
    px_lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aSrc));
    aSrc += 4;
    aLength -= 4;

    if (aLength) {
      // Load pixels 5-7.
      __m128i px_hi = LoadRemainder_SSE2(aSrc, aLength);
      return _mm256_set_m128i(px_lo, px_hi);
    }
  } else {
    // Load pixels 1-3.
    px_lo = LoadRemainder_SSE2(aSrc, aLength);
  }

  return _mm256_castsi128_si256(px_lo);
}

// Store 1-7 pixels from a vector into memory without overwriting.
static MOZ_ALWAYS_INLINE void StoreRemainder_AVX2(uint8_t* aDst, size_t aLength,
                                                  const __m256i& aSrc) {
  // Store first 4 pixels if available.
  __m128i px = _mm256_extracti128_si256(aSrc, 0);
  if (aLength >= 4) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(aDst), px);
    px = _mm256_extracti128_si256(aSrc, 1);
    aDst += 4;
    aLength -= 4;
  }

  // Store remaining 0-3 pixels.
  if (aLength) {
    StoreRemainder_SSE2(aDst, aLength, px);
  }
}

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SWIZZLEAVX2_H_ */
