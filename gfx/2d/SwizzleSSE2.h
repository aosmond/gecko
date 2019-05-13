/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLESSE2_H_
#define MOZILLA_GFX_SWIZZLESSE2_H_

#include <emmintrin.h>

namespace mozilla {
namespace gfx {

// Load 1-3 pixels into a 4 pixel vector.
static MOZ_ALWAYS_INLINE __m128i LoadRemainder_SSE2(const uint8_t* aSrc,
                                                    size_t aLength) {
  __m128i px;
  if (aLength >= 2) {
    // Load first 2 pixels
    px = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(aSrc));
    // Load third pixel
    if (aLength >= 3) {
      px = _mm_unpacklo_epi64(
          px,
          _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc + 2 * 4)));
    }
  } else {
    // Load single pixel
    px = _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc));
  }
  return px;
}

// Store 1-3 pixels from a vector into memory without overwriting.
static MOZ_ALWAYS_INLINE void StoreRemainder_SSE2(uint8_t* aDst, size_t aLength,
                                                  const __m128i& aSrc) {
  if (aLength >= 2) {
    // Store first 2 pixels
    _mm_storel_epi64(reinterpret_cast<__m128i*>(aDst), aSrc);
    // Store third pixel
    if (aLength >= 3) {
      *reinterpret_cast<uint32_t*>(aDst + 2 * 4) =
          _mm_cvtsi128_si32(_mm_srli_si128(aSrc, 2 * 4));
    }
  } else {
    // Store single pixel
    *reinterpret_cast<uint32_t*>(aDst) = _mm_cvtsi128_si32(aSrc);
  }
}

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SWIZZLESSE2_H_ */
