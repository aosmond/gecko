/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLEAVX_H_
#define MOZILLA_GFX_SWIZZLEAVX_H_

#include "SwizzleSSE2.h"
#include <immintrin.h>

namespace mozilla {
namespace gfx {

// Load 1-7 pixels into a 8 pixel vector.
static MOZ_ALWAYS_INLINE __m256i LoadRemainder_AVX(const uint8_t* aSrc,
                                                   size_t aLength) {
  __m128i pxlo, pxhi;
  if (aLength > 4) {
    // Load first 4 pixels
    pxlo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aSrc));
    // Load remaining 1-3 pixels
    pxhi = LoadRemainder_SSE2(aSrc + 4 * 4, aLength - 4);
  } else if (aLength == 4) {
    // Load first 4 pixels
    pxlo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aSrc));
    pxhi = _mm_setzero_si128();
  } else {
    // Load 1-3 pixels
    pxlo = LoadRemainder_SSE2(aSrc, aLength);
    pxhi = _mm_setzero_si128();
  }

  return _mm256_set_m128i(pxhi, pxlo);
}

// Store 1-7 pixels from a vector into memory without overwriting.
static MOZ_ALWAYS_INLINE void StoreRemainder_AVX(uint8_t* aDst, size_t aLength,
                                                 const __m256i& aSrc) {
  __m128i pxlo = _mm256_castsi256_si128(aSrc);
  if (aLength > 4) {
    // Store first 4 pixels
    _mm_storeu_si128(reinterpret_cast<__m128i*>(aDst), pxlo);
    // Store remaining 1-3 pixels
    __m128i pxhi = _mm256_extractf128_si256(aSrc, 1);
    StoreRemainder_SSE2(aDst + 4 * 4, aLength - 4, pxhi);
  } else if (aLength == 4) {
    // Store first 4 pixels
    _mm_storeu_si128(reinterpret_cast<__m128i*>(aDst), pxlo);
  } else {
    // Store remaining 1-3 pixels
    StoreRemainder_SSE2(aDst, aLength, pxlo);
  }
}

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SWIZZLEAVX_H_ */
