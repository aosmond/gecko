/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsJPEGEncoder.h"
#include "prprf.h"
#include "nsString.h"
#include "gfxColor.h"
#include "mozilla/DebugOnly.h"

extern "C" {
#include "jpeglib.h"
}

#include <setjmp.h>
#include "jerror.h"

using namespace mozilla;
using namespace mozilla::gfx;

class nsJPEGEncoderInternal {
  friend class nsJPEGEncoder;

 protected:
  /**
   * Initialize destination. This is called by jpeg_start_compress() before
   * any data is actually written. It must initialize next_output_byte and
   * free_in_buffer. free_in_buffer must be initialized to a positive value.
   */
  static void initDestination(jpeg_compress_struct* cinfo);

  /**
   * This is called whenever the buffer has filled (free_in_buffer reaches
   * zero).  In typical applications, it should write out the *entire* buffer
   * (use the saved start address and buffer length; ignore the current state
   * of next_output_byte and free_in_buffer).  Then reset the pointer & count
   * to the start of the buffer, and return TRUE indicating that the buffer
   * has been dumped.  free_in_buffer must be set to a positive value when
   * TRUE is returned.  A FALSE return should only be used when I/O suspension
   * is desired (this operating mode is discussed in the next section).
   */
  static boolean emptyOutputBuffer(jpeg_compress_struct* cinfo);

  /**
   * Terminate destination --- called by jpeg_finish_compress() after all data
   * has been written.  In most applications, this must flush any data
   * remaining in the buffer.  Use either next_output_byte or free_in_buffer
   * to determine how much data is in the buffer.
   */
  static void termDestination(jpeg_compress_struct* cinfo);

  /**
   * Override the standard error method in the IJG JPEG decoder code. This
   * was mostly copied from nsJPEGDecoder.cpp
   */
  static void errorExit(jpeg_common_struct* cinfo);
};

// used to pass error info through the JPEG library
struct encoder_error_mgr {
  jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

nsJPEGEncoder::nsJPEGEncoder()
    : mReentrantMonitor("nsJPEGEncoder.mReentrantMonitor") {}

nsJPEGEncoder::~nsJPEGEncoder() {}

// nsJPEGEncoder::InitFromData
//
//    One output option is supported: "quality=X" where X is an integer in the
//    range 0-100. Higher values for X give better quality.
//
//    Transparency is always discarded.

nsresult nsJPEGEncoder::InitFromSurfaceData(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  NS_ENSURE_ARG(aData);

  // can't initialize more than once
  if (HasBuffer()) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  nsresult rv =
      PrepareRowPipeline(aFormat, aFlags, SurfaceFormat::R8G8B8, aSize);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // options: we only have one option so this is easy
  int quality = 92;
  if (aOptions.Length() > 0) {
    // have options string
    const nsString qualityPrefix(NS_LITERAL_STRING("quality="));
    if (aOptions.Length() > qualityPrefix.Length() &&
        StringBeginsWith(aOptions, qualityPrefix)) {
      // have quality string
      nsCString value =
          NS_ConvertUTF16toUTF8(Substring(aOptions, qualityPrefix.Length()));
      int newquality = -1;
      if (PR_sscanf(value.get(), "%d", &newquality) == 1) {
        if (newquality >= 0 && newquality <= 100) {
          quality = newquality;
        } else {
          NS_WARNING(
              "Quality value out of range, should be 0-100,"
              " using default");
        }
      } else {
        NS_WARNING(
            "Quality value invalid, should be integer 0-100,"
            " using default");
      }
    } else {
      return NS_ERROR_INVALID_ARG;
    }
  }

  jpeg_compress_struct cinfo;

  // We set up the normal JPEG error routines, then override error_exit.
  // This must be done before the call to create_compress
  encoder_error_mgr errmgr;
  cinfo.err = jpeg_std_error(&errmgr.pub);
  errmgr.pub.error_exit = nsJPEGEncoderInternal::errorExit;
  // Establish the setjmp return context for my_error_exit to use.
  if (setjmp(errmgr.setjmp_buffer)) {
    // If we get here, the JPEG code has signaled an error.
    // We need to clean up the JPEG object, close the input file, and return.
    return NS_ERROR_FAILURE;
  }

  jpeg_create_compress(&cinfo);
  cinfo.image_width = aSize.width;
  cinfo.image_height = aSize.height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  cinfo.data_precision = 8;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, 1);  // quality here is 0-100
  if (quality >= 90) {
    int i;
    for (i = 0; i < MAX_COMPONENTS; i++) {
      cinfo.comp_info[i].h_samp_factor = 1;
      cinfo.comp_info[i].v_samp_factor = 1;
    }
  }

  // set up the destination manager
  jpeg_destination_mgr destmgr;
  destmgr.init_destination = nsJPEGEncoderInternal::initDestination;
  destmgr.empty_output_buffer = nsJPEGEncoderInternal::emptyOutputBuffer;
  destmgr.term_destination = nsJPEGEncoderInternal::termDestination;
  cinfo.dest = &destmgr;
  cinfo.client_data = this;

  jpeg_start_compress(&cinfo, 1);

  while (cinfo.next_scanline < cinfo.image_height) {
    const uint8_t* row =
        PrepareRow(aData, aStride, aSize.width, cinfo.next_scanline);
    jpeg_write_scanlines(&cinfo, const_cast<uint8_t**>(&row), 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  return ImageEncoder::EndImageEncode();
}

nsresult nsJPEGEncoder::StartSurfaceDataEncode(const IntSize& aSize,
                                               SurfaceFormat aFormat,
                                               DataSurfaceFlags aFlags,
                                               const nsAString& aOptions) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsJPEGEncoder::AddSurfaceDataFrame(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsJPEGEncoder::EndImageEncode() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsJPEGEncoder::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                            uint32_t aCount, uint32_t* _retval) {
  // Avoid another thread reallocing the buffer underneath us
  ReentrantMonitorAutoEnter autoEnter(mReentrantMonitor);
  return ImageEncoder::ReadSegments(aWriter, aClosure, aCount, _retval);
}

void nsJPEGEncoder::NotifyListener() {
  // We might call this function on multiple threads (any threads that call
  // AsyncWait and any that do encoding) so we lock to avoid notifying the
  // listener twice about the same data (which generally leads to a truncated
  // image).
  ReentrantMonitorAutoEnter autoEnter(mReentrantMonitor);
  return ImageEncoder::NotifyListener();
}

/* static */
void nsJPEGEncoderInternal::initDestination(jpeg_compress_struct* cinfo) {
  nsJPEGEncoder* that = static_cast<nsJPEGEncoder*>(cinfo->client_data);
  NS_ASSERTION(!that->HasBuffer(), "Image buffer already initialized");

  that->AllocateBuffer(8192);
  cinfo->dest->next_output_byte = that->BufferHead();
  cinfo->dest->free_in_buffer = that->RemainingBytesToWrite();
}

/* static */
boolean nsJPEGEncoderInternal::emptyOutputBuffer(jpeg_compress_struct* cinfo) {
  nsJPEGEncoder* that = static_cast<nsJPEGEncoder*>(cinfo->client_data);
  NS_ASSERTION(that->HasBuffer(), "No buffer to empty!");

  // When we're reallocing the buffer we need to take the lock to ensure
  // that nobody is trying to read from the buffer we are destroying
  ReentrantMonitorAutoEnter autoEnter(that->mReentrantMonitor);

  nsresult rv = that->AdvanceBuffer(that->RemainingBytesToWrite());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // expand buffer, just double size each time
  rv = that->ReallocateBuffer(that->BufferSize() * 2);
  if (NS_FAILED(rv)) {
    // can't resize, just zero (this will keep us from writing more)
    that->ReleaseBuffer();

    // This seems to be the only way to do errors through the JPEG library.  We
    // pass an nsresult masquerading as an int, which works because the
    // setjmp() caller casts it back.
    longjmp(((encoder_error_mgr*)(cinfo->err))->setjmp_buffer,
            static_cast<int>(NS_ERROR_OUT_OF_MEMORY));
  }

  cinfo->dest->next_output_byte = that->BufferHead();
  cinfo->dest->free_in_buffer = that->RemainingBytesToWrite();
  return 1;
}

/* static */
void nsJPEGEncoderInternal::termDestination(jpeg_compress_struct* cinfo) {
  nsJPEGEncoder* that = static_cast<nsJPEGEncoder*>(cinfo->client_data);
  if (!that->HasBuffer()) {
    return;
  }
  uint32_t len = cinfo->dest->next_output_byte - that->BufferHead();
  DebugOnly<nsresult> rv = that->AdvanceBuffer(len);
  NS_ASSERTION(NS_SUCCEEDED(rv),
               "JPEG library busted, got a bad image buffer size");
  that->NotifyListener();
}

/* static */
void nsJPEGEncoderInternal::errorExit(jpeg_common_struct* cinfo) {
  nsresult error_code;
  encoder_error_mgr* err = (encoder_error_mgr*)cinfo->err;

  // Convert error to a browser error code
  switch (cinfo->err->msg_code) {
    case JERR_OUT_OF_MEMORY:
      error_code = NS_ERROR_OUT_OF_MEMORY;
      break;
    default:
      error_code = NS_ERROR_FAILURE;
  }

  // Return control to the setjmp point.  We pass an nsresult masquerading as
  // an int, which works because the setjmp() caller casts it back.
  longjmp(err->setjmp_buffer, static_cast<int>(error_code));
}
