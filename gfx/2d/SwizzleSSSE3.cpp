/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SwizzleSSE2.h"
#include "Swizzle.h"

#include <tmmintrin.h>

namespace mozilla {
namespace gfx {

template <bool aSwapRB>
void UnpackRowRGB24_SSSE3(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  int32_t alignedRow = 3 * (aLength & ~3);
  int32_t remainder = aLength & 3;

  __m128i mask;
  if (aSwapRB) {
    mask = _mm_set_epi8(15, 9, 10, 11, 14, 6, 7, 8, 13, 3, 4, 5, 12, 0, 1, 2);
  } else {
    mask = _mm_set_epi8(15, 11, 10, 9, 14, 8, 7, 6, 13, 5, 4, 3, 12, 2, 1, 0);
  }

  __m128i alpha = _mm_set1_epi32(0xFF000000);

  // Process all 4-pixel chunks as one vector.
  for (const uint8_t* end = aSrc + alignedRow; aSrc < end;) {
    __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aSrc));
    px = _mm_shuffle_epi8(px, mask);
    px = _mm_or_si128(px, alpha);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(aDst), px);
    aSrc += 4 * 3;
    aDst += 4 * 4;
  }

  // Handle any 1-3 remaining pixels.
  if (remainder) {
    __m128i px = LoadRemainder_SSE2(aSrc, remainder);
    _mm_shuffle_epi8(px, mask);
    _mm_or_si128(px, alpha);
    StoreRemainder_SSE2(aDst, remainder, px);
  }
}

// Force instantiation of swizzle variants here.
template void UnpackRowRGB24_SSSE3<false>(const uint8_t*, uint8_t*, int32_t);
template void UnpackRowRGB24_SSSE3<true>(const uint8_t*, uint8_t*, int32_t);

}  // namespace gfx
}  // namespace mozilla
