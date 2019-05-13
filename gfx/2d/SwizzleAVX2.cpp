/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SwizzleAVX2.h"
#include "Swizzle.h"

#include <tmmintrin.h>

namespace mozilla {
namespace gfx {

template <bool aSwapRB>
void UnpackRowRGB24_AVX2(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  const int32_t alignedRow = 3 * (aLength & ~7);
  const int32_t remainder = aLength & 7;

  // Used to shuffle the two final 32-bit words which we ignore into the last
  // 32-bit word of each 128-bit lane, such that
  //   RGBR GBRG BRGB RGBR GBRG BRGB RGBR GBRG
  //   BRGB RGBR GBRG BRGB ZZZZ ZZZZ ZZZZ ZZZZ
  // becomes
  //   RGBR GBRG BRGB RGBR GBRG BRGB ZZZZ ZZZZ
  //   RGBR GBRG BRGB RGBR GBRG BRGB ZZZZ ZZZZ
  const __m256i discardMask = _mm256_set_epi32(7, 5, 4, 3, 6, 2, 1, 0);

  // Used to shuffle 8-bit words within a 128-bit lane, such that we transform
  //   RGBR GBRG BRGB RGBR GBRG BRGB ZZZZ ZZZZ
  // into
  //   RGBZ RGBZ RGBZ RGBZ RGBZ RGBZ RGBZ RGBZ
  // or
  //   BGRZ BGRZ BGRZ BGRZ BGRZ BGRZ BGRZ BGRZ
  const __m256i colorMask =
      aSwapRB ? _mm256_set_epi8(15, 9, 10, 11, 14, 6, 7, 8, 13, 3, 4, 5, 12, 0,
                                1, 2, 15, 9, 10, 11, 14, 6, 7, 8, 13, 3, 4, 5,
                                12, 0, 1, 2)
              : _mm256_set_epi8(15, 11, 10, 9, 14, 8, 7, 6, 13, 5, 4, 3, 12, 2,
                                1, 0, 15, 11, 10, 9, 14, 8, 7, 6, 13, 5, 4, 3,
                                12, 2, 1, 0);

  // Used to transform RGBZ/BGRZ to RGBX/BGRX, or force the alpha opaque.
  const __m256i alphaMask = _mm256_set1_epi32(0xFF000000);

  // Process all 8-pixel chunks as one vector.
  for (const uint8_t* end = aSrc + alignedRow; aSrc < end;) {
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aSrc));
    px = _mm256_permutevar8x32_epi32(px, discardMask);
    px = _mm256_shuffle_epi8(px, colorMask);
    px = _mm256_or_si256(px, alphaMask);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(aDst), px);
    aSrc += 8 * 3;
    aDst += 8 * 4;
  }

  // Handle any 1-7 remaining pixels.
  if (remainder) {
    __m256i px = LoadRemainder_AVX2(aSrc, remainder);
    px = _mm256_permutevar8x32_epi32(px, discardMask);
    px = _mm256_shuffle_epi8(px, colorMask);
    px = _mm256_or_si256(px, alphaMask);
    StoreRemainder_AVX2(aDst, remainder, px);
  }
}

// Force instantiation of swizzle variants here.
template void UnpackRowRGB24_AVX2<false>(const uint8_t*, uint8_t*, int32_t);
template void UnpackRowRGB24_AVX2<true>(const uint8_t*, uint8_t*, int32_t);

}  // namespace gfx
}  // namespace mozilla
