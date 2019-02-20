/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This file replaces the jpeg_simd_cpu_support method originally
 * implemented in simd/i386/jsimdcpu.asm, due to faulty feature detection
 * on some i386 processors. */

#include "mozilla/SSE.h"

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jdct.h"
#include "jchuff.h"

extern "C" {
#include "simd/jsimd.h"

EXTERN(unsigned int) jpeg_simd_cpu_support() {
  unsigned int flags = JSIMD_NONE;
  flags |= mozilla::supports_sse() ? JSIMD_SSE : 0;
  flags |= mozilla::supports_sse2() ? JSIMD_SSE2 : 0;
  flags |= mozilla::supports_mmx() ? JSIMD_MMX : 0;
  flags |= mozilla::supports_avx2() ? JSIMD_AVX2 : 0;
  return flags;
}
}
