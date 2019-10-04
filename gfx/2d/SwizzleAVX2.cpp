/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Swizzle.h"
#include "SwizzleAVX.h"

#include <immintrin.h>
#include <tmmintrin.h>

namespace mozilla {
namespace gfx {

// Premultiply vector of 8 pixels using splayed math.
template <bool aSwapRB, bool aOpaqueAlpha>
static MOZ_ALWAYS_INLINE __m256i PremultiplyVector_AVX2(const __m256i& aSrc) {
  // Isolate R and B with mask.
  const __m256i mask = _mm256_set1_epi32(0x00FF00FF);
  __m256i rb = _mm256_and_si256(mask, aSrc);
  // Swap R and B if necessary.
  if (aSwapRB) {
    rb = _mm256_shufflelo_epi16(rb, _MM_SHUFFLE(2, 3, 0, 1));
    rb = _mm256_shufflehi_epi16(rb, _MM_SHUFFLE(2, 3, 0, 1));
  }
  // Isolate G and A by shifting down to bottom of word.
  __m256i ga = _mm256_srli_epi16(aSrc, 8);

  // Duplicate alphas to get vector of A1 A1 A2 A2 A3 A3 A4 A4
  __m256i alphas = _mm256_shufflelo_epi16(ga, _MM_SHUFFLE(3, 3, 1, 1));
  alphas = _mm256_shufflehi_epi16(alphas, _MM_SHUFFLE(3, 3, 1, 1));

  // rb = rb*a + 255; rb += rb >> 8;
  rb = _mm256_add_epi16(_mm256_mullo_epi16(rb, alphas), mask);
  rb = _mm256_add_epi16(rb, _mm256_srli_epi16(rb, 8));

  // If format is not opaque, force A to 255 so that A*alpha/255 = alpha
  if (!aOpaqueAlpha) {
    ga = _mm256_or_si256(ga, _mm256_set1_epi32(0x00FF0000));
  }
  // ga = ga*a + 255; ga += ga >> 8;
  ga = _mm256_add_epi16(_mm256_mullo_epi16(ga, alphas), mask);
  ga = _mm256_add_epi16(ga, _mm256_srli_epi16(ga, 8));
  // If format is opaque, force output A to be 255.
  if (aOpaqueAlpha) {
    ga = _mm256_or_si256(ga, _mm256_set1_epi32(0xFF000000));
  }

  // Combine back to final pixel with (rb >> 8) | (ga & 0xFF00FF00)
  rb = _mm256_srli_epi16(rb, 8);
  ga = _mm256_andnot_si256(mask, ga);
  return _mm256_or_si256(rb, ga);
}

// Premultiply vector of aAlignedRow + aRemainder pixels.
template <bool aSwapRB, bool aOpaqueAlpha>
static MOZ_ALWAYS_INLINE void PremultiplyChunk_AVX2(const uint8_t*& aSrc,
                                                    uint8_t*& aDst,
                                                    int32_t aAlignedRow,
                                                    int32_t aRemainder) {
  // Process all 8-pixel chunks as one vector.
  for (const uint8_t* end = aSrc + aAlignedRow; aSrc < end;) {
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aSrc));
    px = PremultiplyVector_AVX2<aSwapRB, aOpaqueAlpha>(px);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(aDst), px);
    aSrc += 8 * 4;
    aDst += 8 * 4;
  }

  // Handle any 1-7 remaining pixels.
  if (aRemainder) {
    __m256i px = LoadRemainder_AVX(aSrc, aRemainder);
    px = PremultiplyVector_AVX2<aSwapRB, aOpaqueAlpha>(px);
    StoreRemainder_AVX(aDst, aRemainder, px);
  }
}

// Premultiply vector of aLength pixels.
template <bool aSwapRB, bool aOpaqueAlpha>
void PremultiplyRow_AVX2(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  int32_t alignedRow = 4 * (aLength & ~7);
  int32_t remainder = aLength & 7;
  PremultiplyChunk_AVX2<aSwapRB, aOpaqueAlpha>(aSrc, aDst, alignedRow,
                                               remainder);
}

template <bool aSwapRB, bool aOpaqueAlpha>
void Premultiply_AVX2(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                      int32_t aDstGap, IntSize aSize) {
  int32_t alignedRow = 4 * (aSize.width & ~7);
  int32_t remainder = aSize.width & 7;
  // Fold remainder into stride gap.
  aSrcGap += 4 * remainder;
  aDstGap += 4 * remainder;

  for (int32_t height = aSize.height; height > 0; height--) {
    PremultiplyChunk_AVX2<aSwapRB, aOpaqueAlpha>(aSrc, aDst, alignedRow,
                                                 remainder);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

// Force instantiation of premultiply variants here.
template void PremultiplyRow_AVX2<false, false>(const uint8_t*, uint8_t*,
                                                int32_t);
template void PremultiplyRow_AVX2<false, true>(const uint8_t*, uint8_t*,
                                               int32_t);
template void PremultiplyRow_AVX2<true, false>(const uint8_t*, uint8_t*,
                                               int32_t);
template void PremultiplyRow_AVX2<true, true>(const uint8_t*, uint8_t*,
                                              int32_t);
template void Premultiply_AVX2<false, false>(const uint8_t*, int32_t, uint8_t*,
                                             int32_t, IntSize);
template void Premultiply_AVX2<false, true>(const uint8_t*, int32_t, uint8_t*,
                                            int32_t, IntSize);
template void Premultiply_AVX2<true, false>(const uint8_t*, int32_t, uint8_t*,
                                            int32_t, IntSize);
template void Premultiply_AVX2<true, true>(const uint8_t*, int32_t, uint8_t*,
                                           int32_t, IntSize);

// Swizzle a vector of 8 pixels providing swaps and opaquifying.
template <bool aSwapRB, bool aOpaqueAlpha>
static MOZ_ALWAYS_INLINE __m256i SwizzleVector_AVX2(const __m256i& aSrc) {
  // Isolate R and B.
  __m256i rb = _mm256_and_si256(aSrc, _mm256_set1_epi32(0x00FF00FF));
  // Swap R and B.
  rb = _mm256_shufflelo_epi16(rb, _MM_SHUFFLE(2, 3, 0, 1));
  rb = _mm256_shufflehi_epi16(rb, _MM_SHUFFLE(2, 3, 0, 1));
  // Isolate G and A.
  __m256i ga = _mm256_and_si256(aSrc, _mm256_set1_epi32(0xFF00FF00));
  // Force alpha to 255 if necessary.
  if (aOpaqueAlpha) {
    ga = _mm256_or_si256(ga, _mm256_set1_epi32(0xFF000000));
  }
  // Combine everything back together.
  return _mm256_or_si256(rb, ga);
}

template <bool aSwapRB, bool aOpaqueAlpha>
static MOZ_ALWAYS_INLINE void SwizzleChunk_AVX2(const uint8_t*& aSrc,
                                                uint8_t*& aDst,
                                                int32_t aAlignedRow,
                                                int32_t aRemainder) {
  // Process all 4-pixel chunks as one vector.
  for (const uint8_t* end = aSrc + aAlignedRow; aSrc < end;) {
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aSrc));
    px = SwizzleVector_AVX2<aSwapRB, aOpaqueAlpha>(px);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(aDst), px);
    aSrc += 8 * 4;
    aDst += 8 * 4;
  }

  // Handle any 1-7 remaining pixels.
  if (aRemainder) {
    __m256i px = LoadRemainder_AVX(aSrc, aRemainder);
    px = SwizzleVector_AVX2<aSwapRB, aOpaqueAlpha>(px);
    StoreRemainder_AVX(aDst, aRemainder, px);
  }
}

template <bool aSwapRB, bool aOpaqueAlpha>
void SwizzleRow_AVX2(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  int32_t alignedRow = 4 * (aLength & ~7);
  int32_t remainder = aLength & 7;
  SwizzleChunk_AVX2<aSwapRB, aOpaqueAlpha>(aSrc, aDst, alignedRow, remainder);
}

template <bool aSwapRB, bool aOpaqueAlpha>
void Swizzle_AVX2(const uint8_t* aSrc, int32_t aSrcGap, uint8_t* aDst,
                  int32_t aDstGap, IntSize aSize) {
  int32_t alignedRow = 4 * (aSize.width & ~7);
  int32_t remainder = aSize.width & 7;
  // Fold remainder into stride gap.
  aSrcGap += 4 * remainder;
  aDstGap += 4 * remainder;

  for (int32_t height = aSize.height; height > 0; height--) {
    SwizzleChunk_AVX2<aSwapRB, aOpaqueAlpha>(aSrc, aDst, alignedRow, remainder);
    aSrc += aSrcGap;
    aDst += aDstGap;
  }
}

// Force instantiation of swizzle variants here.
template void SwizzleRow_AVX2<true, false>(const uint8_t*, uint8_t*, int32_t);
template void SwizzleRow_AVX2<true, true>(const uint8_t*, uint8_t*, int32_t);
template void Swizzle_AVX2<true, false>(const uint8_t*, int32_t, uint8_t*,
                                        int32_t, IntSize);
template void Swizzle_AVX2<true, true>(const uint8_t*, int32_t, uint8_t*,
                                       int32_t, IntSize);

template <bool aSwapRB>
void UnpackRowRGB24_SSSE3(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength);

template <bool aSwapRB>
void UnpackRowRGB24_AVX2(const uint8_t* aSrc, uint8_t* aDst, int32_t aLength) {
  // Because this implementation will read an additional 8 bytes of data that
  // is ignored and masked over, we cannot use the accelerated version for the
  // last 1-10 pixels (3-30 bytes remaining) to guarantee we don't access memory
  // outside the buffer (we read in 32 byte chunks).
  if (aLength < 11) {
    UnpackRowRGB24_SSSE3<aSwapRB>(aSrc, aDst, aLength);
    return;
  }

  // Because we are expanding, we can only process the data back to front in
  // case we are performing this in place.
  int32_t alignedRow = (aLength - 4) & ~7;
  int32_t remainder = aLength - alignedRow;

  const uint8_t* src = aSrc + alignedRow * 3;
  uint8_t* dst = aDst + alignedRow * 4;

  // Handle any 3-10 remaining pixels.
  UnpackRowRGB24_SSSE3<aSwapRB>(src, dst, remainder);

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
  src -= 8 * 3;
  dst -= 8 * 4;
  while (src >= aSrc) {
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
    px = _mm256_permutevar8x32_epi32(px, discardMask);
    px = _mm256_shuffle_epi8(px, colorMask);
    px = _mm256_or_si256(px, alphaMask);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), px);
    src -= 8 * 3;
    dst -= 8 * 4;
  }
}

// Force instantiation of swizzle variants here.
template void UnpackRowRGB24_AVX2<false>(const uint8_t*, uint8_t*, int32_t);
template void UnpackRowRGB24_AVX2<true>(const uint8_t*, uint8_t*, int32_t);

}  // namespace gfx
}  // namespace mozilla
