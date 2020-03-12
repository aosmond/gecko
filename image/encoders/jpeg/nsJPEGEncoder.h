/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_encoders_jpeg_nsJPEGEncoder_h
#define mozilla_image_encoders_jpeg_nsJPEGEncoder_h

#include "mozilla/image/Encoder.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/Attributes.h"

#include "nsCOMPtr.h"

struct jpeg_compress_struct;
struct jpeg_common_struct;

// Provides JPEG encoding functionality. Use InitFromData() to do the
// encoding. See that function definition for encoding options.
class nsJPEGEncoderInternal;

class nsJPEGEncoder final : public mozilla::image::ImageEncoder {
  friend class nsJPEGEncoderInternal;
  typedef mozilla::ReentrantMonitor ReentrantMonitor;

 public:
  nsJPEGEncoder();

  // From ImageEncoder
  nsresult InitFromSurfaceData(const uint8_t* aData,
                               const mozilla::gfx::IntSize& aSize,
                               int32_t aStride,
                               mozilla::gfx::SurfaceFormat aFormat,
                               mozilla::gfx::DataSurfaceFlags aFlags,
                               const nsAString& aOptions) override;

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

 private:
  ~nsJPEGEncoder() override;

 protected:
  void NotifyListener() override;

  // nsJPEGEncoder is designed to allow one thread to pump data into it while
  // another reads from it.  We lock to ensure that the buffer remains
  // append-only while we read from it (that it is not realloced) and to ensure
  // that only one thread dispatches a callback for each call to AsyncWait.
  ReentrantMonitor mReentrantMonitor;
};

#endif  // mozilla_image_encoders_jpeg_nsJPEGEncoder_h
