/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_encoders_png_nsPNGEncoder_h
#define mozilla_image_encoders_png_nsPNGEncoder_h

#include <png.h>

#include "mozilla/image/Encoder.h"
#include "mozilla/Attributes.h"
#include "mozilla/ReentrantMonitor.h"

// Provides PNG encoding functionality. Use InitFromData() to do the
// encoding. See that function definition for encoding options.

class nsPNGEncoder final : public mozilla::image::ImageEncoder {
  typedef mozilla::ReentrantMonitor ReentrantMonitor;

 public:
  nsPNGEncoder();

  // From ImageEncoder
  nsresult StartSurfaceDataEncode(const mozilla::gfx::IntSize& aSize,
                                  mozilla::gfx::SurfaceFormat aFormat,
                                  mozilla::gfx::DataSurfaceFlags aFlags,
                                  const nsAString& outputOptions) override;

  nsresult AddSurfaceDataFrame(const uint8_t* aData,
                               const mozilla::gfx::IntSize& aSize,
                               int32_t aStride,
                               mozilla::gfx::SurfaceFormat aFormat,
                               mozilla::gfx::DataSurfaceFlags aFlags,
                               const nsAString& aOptions) override;

  NS_IMETHOD EndImageEncode(void) override;

  NS_IMETHOD ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                          uint32_t aCount, uint32_t* _retval) override;

 protected:
  ~nsPNGEncoder() override;
  nsresult ParseOptions(const nsAString& aOptions, bool* useTransparency,
                        bool* skipFirstFrame, uint32_t* numAnimatedFrames,
                        uint32_t* numIterations, uint32_t* frameDispose,
                        uint32_t* frameBlend, uint32_t* frameDelay,
                        uint32_t* offsetX, uint32_t* offsetY);
  static void WarningCallback(png_structp png_ptr, png_const_charp warning_msg);
  static void ErrorCallback(png_structp png_ptr, png_const_charp error_msg);
  static void WriteCallback(png_structp png, png_bytep data, png_size_t size);
  void NotifyListener() override;

  png_struct* mPNG;
  png_info* mPNGinfo;

  bool mIsAnimation;

  // nsPNGEncoder is designed to allow one thread to pump data into it while
  // another reads from it.  We lock to ensure that the buffer remains
  // append-only while we read from it (that it is not realloced) and to
  // ensure that only one thread dispatches a callback for each call to
  // AsyncWait.
  ReentrantMonitor mReentrantMonitor;
};
#endif  // mozilla_image_encoders_png_nsPNGEncoder_h
