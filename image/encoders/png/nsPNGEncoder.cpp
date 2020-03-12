/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"
#include "nsCRT.h"
#include "nsPNGEncoder.h"
#include "nsString.h"
#include "prprf.h"
#include "mozilla/CheckedInt.h"

using namespace mozilla;
using namespace mozilla::gfx;

static LazyLogModule sPNGEncoderLog("PNGEncoder");

nsPNGEncoder::nsPNGEncoder()
    : mPNG(nullptr),
      mPNGinfo(nullptr),
      mIsAnimation(false),
      mReentrantMonitor("nsPNGEncoder.mReentrantMonitor") {}

nsPNGEncoder::~nsPNGEncoder() {
  // don't leak if EndImageEncode wasn't called
  if (mPNG) {
    png_destroy_write_struct(&mPNG, &mPNGinfo);
  }
}

// nsPNGEncoder::StartImageEncode
//
//    One output option is supported: "transparency=none" means that the
//    output PNG will not have an alpha channel, even if the input does.
//
//    Based partially on gfx/cairo/cairo/src/cairo-png.c
//    See also media/libpng/libpng-manual.txt
nsresult nsPNGEncoder::StartSurfaceDataEncode(const IntSize& aSize,
                                              SurfaceFormat aFormat,
                                              DataSurfaceFlags aFlags,
                                              const nsAString& aOptions) {
  bool useTransparency = true, skipFirstFrame = false;
  uint32_t numFrames = 1;
  uint32_t numPlays = 0;  // For animations, 0 == forever

  // can't initialize more than once
  if (HasBuffer()) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // parse and check any provided output options
  nsresult rv =
      ParseOptions(aOptions, &useTransparency, &skipFirstFrame, &numFrames,
                   &numPlays, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (rv != NS_OK) {
    return rv;
  }

#ifdef PNG_APNG_SUPPORTED
  if (numFrames > 1) {
    mIsAnimation = true;
  }

#endif

  // initialize
  mPNG = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, ErrorCallback,
                                 WarningCallback);
  if (!mPNG) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mPNGinfo = png_create_info_struct(mPNG);
  if (!mPNGinfo) {
    png_destroy_write_struct(&mPNG, nullptr);
    return NS_ERROR_FAILURE;
  }

  // libpng's error handler jumps back here upon an error.
  // Note: It's important that all png_* callers do this, or errors
  // will result in a corrupt time-warped stack.
  if (setjmp(png_jmpbuf(mPNG))) {
    png_destroy_write_struct(&mPNG, &mPNGinfo);
    return NS_ERROR_FAILURE;
  }

  // Set up to read the data into our image buffer, start out with an 8K
  // estimated size. Note: we don't have to worry about freeing this data
  // in this function. It will be freed on object destruction.
  rv = AllocateBuffer(8192);
  if (NS_FAILED(rv)) {
    png_destroy_write_struct(&mPNG, &mPNGinfo);
    return rv;
  }

  // set our callback for libpng to give us the data
  png_set_write_fn(mPNG, this, WriteCallback, nullptr);

  // include alpha?
  int colorType;
  if (!IsOpaque(aFormat) && useTransparency) {
    colorType = PNG_COLOR_TYPE_RGB_ALPHA;
  } else {
    colorType = PNG_COLOR_TYPE_RGB;
  }

  png_set_IHDR(mPNG, mPNGinfo, aSize.width, aSize.height, 8, colorType,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

#ifdef PNG_APNG_SUPPORTED
  if (mIsAnimation) {
    png_set_first_frame_is_hidden(mPNG, mPNGinfo, skipFirstFrame);
    png_set_acTL(mPNG, mPNGinfo, numFrames, numPlays);
  }
#endif

  // XXX: support PLTE, gAMA, tRNS, bKGD?

  png_write_info(mPNG, mPNGinfo);

  return NS_OK;
}

nsresult nsPNGEncoder::AddSurfaceDataFrame(
    const uint8_t* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat, DataSurfaceFlags aFlags, const nsAString& aOptions) {
  bool useTransparency = true;
  uint32_t delay_ms = 500;
#ifdef PNG_APNG_SUPPORTED
  uint32_t dispose_op = PNG_DISPOSE_OP_NONE;
  uint32_t blend_op = PNG_BLEND_OP_SOURCE;
#else
  uint32_t dispose_op;
  uint32_t blend_op;
#endif
  uint32_t x_offset = 0, y_offset = 0;

  // must be initialized
  if (!HasBuffer()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // EndImageEncode was done, or some error occurred earlier
  if (!mPNG) {
    return NS_BASE_STREAM_CLOSED;
  }

  // libpng's error handler jumps back here upon an error.
  if (setjmp(png_jmpbuf(mPNG))) {
    png_destroy_write_struct(&mPNG, &mPNGinfo);
    return NS_ERROR_FAILURE;
  }

  // parse and check any provided output options
  nsresult rv =
      ParseOptions(aOptions, &useTransparency, nullptr, nullptr, nullptr,
                   &dispose_op, &blend_op, &delay_ms, &x_offset, &y_offset);
  if (rv != NS_OK) {
    return rv;
  }

  SurfaceFormat outFormat;
  if (!IsOpaque(aFormat) && useTransparency) {
    outFormat = SurfaceFormat::R8G8B8A8;
  } else {
    outFormat = SurfaceFormat::R8G8B8;
  }

  rv = PrepareRowPipeline(aFormat, aFlags, outFormat, aSize);
  if (NS_FAILED(rv)) {
    return rv;
  }

#ifdef PNG_APNG_SUPPORTED
  if (mIsAnimation) {
    // XXX the row pointers arg (#3) is unused, can it be removed?
    png_write_frame_head(mPNG, mPNGinfo, nullptr, aSize.width, aSize.height,
                         x_offset, y_offset, delay_ms, 1000, dispose_op,
                         blend_op);
  }
#endif

#ifdef PNG_WRITE_FILTER_SUPPORTED
  png_set_filter(mPNG, PNG_FILTER_TYPE_BASE, PNG_FILTER_VALUE_NONE);
#endif

  for (int32_t y = 0; y < aSize.height; y++) {
    const uint8_t* row = PrepareRow(aData, aStride, aSize.width, y);
    png_write_row(mPNG, row);
  }

#ifdef PNG_APNG_SUPPORTED
  if (mIsAnimation) {
    png_write_frame_tail(mPNG, mPNGinfo);
  }
#endif

  return NS_OK;
}

NS_IMETHODIMP
nsPNGEncoder::EndImageEncode() {
  // must be initialized
  if (!HasBuffer()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // EndImageEncode has already been called, or some error
  // occurred earlier
  if (!mPNG) {
    return NS_BASE_STREAM_CLOSED;
  }

  // libpng's error handler jumps back here upon an error.
  if (setjmp(png_jmpbuf(mPNG))) {
    png_destroy_write_struct(&mPNG, &mPNGinfo);
    return NS_ERROR_FAILURE;
  }

  png_write_end(mPNG, mPNGinfo);
  png_destroy_write_struct(&mPNG, &mPNGinfo);

  return ImageEncoder::EndImageEncode();
}

nsresult nsPNGEncoder::ParseOptions(const nsAString& aOptions,
                                    bool* useTransparency, bool* skipFirstFrame,
                                    uint32_t* numFrames, uint32_t* numPlays,
                                    uint32_t* frameDispose,
                                    uint32_t* frameBlend, uint32_t* frameDelay,
                                    uint32_t* offsetX, uint32_t* offsetY) {
#ifdef PNG_APNG_SUPPORTED
  // Make a copy of aOptions, because strtok() will modify it.
  nsAutoCString optionsCopy;
  optionsCopy.Assign(NS_ConvertUTF16toUTF8(aOptions));
  char* options = optionsCopy.BeginWriting();

  while (char* token = nsCRT::strtok(options, ";", &options)) {
    // If there's an '=' character, split the token around it.
    char* equals = token;
    char* value = nullptr;
    while (*equals != '=' && *equals) {
      ++equals;
    }
    if (*equals == '=') {
      value = equals + 1;
    }

    if (value) {
      *equals = '\0';  // temporary null
    }

    // transparency=[yes|no|none]
    if (nsCRT::strcmp(token, "transparency") == 0 && useTransparency) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (nsCRT::strcmp(value, "none") == 0 ||
          nsCRT::strcmp(value, "no") == 0) {
        *useTransparency = false;
      } else if (nsCRT::strcmp(value, "yes") == 0) {
        *useTransparency = true;
      } else {
        return NS_ERROR_INVALID_ARG;
      }

      // skipfirstframe=[yes|no]
    } else if (nsCRT::strcmp(token, "skipfirstframe") == 0 && skipFirstFrame) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (nsCRT::strcmp(value, "no") == 0) {
        *skipFirstFrame = false;
      } else if (nsCRT::strcmp(value, "yes") == 0) {
        *skipFirstFrame = true;
      } else {
        return NS_ERROR_INVALID_ARG;
      }

      // frames=#
    } else if (nsCRT::strcmp(token, "frames") == 0 && numFrames) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (PR_sscanf(value, "%u", numFrames) != 1) {
        return NS_ERROR_INVALID_ARG;
      }

      // frames=0 is nonsense.
      if (*numFrames == 0) {
        return NS_ERROR_INVALID_ARG;
      }

      // plays=#
    } else if (nsCRT::strcmp(token, "plays") == 0 && numPlays) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      // plays=0 to loop forever, otherwise play sequence specified
      // number of times
      if (PR_sscanf(value, "%u", numPlays) != 1) {
        return NS_ERROR_INVALID_ARG;
      }

      // dispose=[none|background|previous]
    } else if (nsCRT::strcmp(token, "dispose") == 0 && frameDispose) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (nsCRT::strcmp(value, "none") == 0) {
        *frameDispose = PNG_DISPOSE_OP_NONE;
      } else if (nsCRT::strcmp(value, "background") == 0) {
        *frameDispose = PNG_DISPOSE_OP_BACKGROUND;
      } else if (nsCRT::strcmp(value, "previous") == 0) {
        *frameDispose = PNG_DISPOSE_OP_PREVIOUS;
      } else {
        return NS_ERROR_INVALID_ARG;
      }

      // blend=[source|over]
    } else if (nsCRT::strcmp(token, "blend") == 0 && frameBlend) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (nsCRT::strcmp(value, "source") == 0) {
        *frameBlend = PNG_BLEND_OP_SOURCE;
      } else if (nsCRT::strcmp(value, "over") == 0) {
        *frameBlend = PNG_BLEND_OP_OVER;
      } else {
        return NS_ERROR_INVALID_ARG;
      }

      // delay=# (in ms)
    } else if (nsCRT::strcmp(token, "delay") == 0 && frameDelay) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (PR_sscanf(value, "%u", frameDelay) != 1) {
        return NS_ERROR_INVALID_ARG;
      }

      // xoffset=#
    } else if (nsCRT::strcmp(token, "xoffset") == 0 && offsetX) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (PR_sscanf(value, "%u", offsetX) != 1) {
        return NS_ERROR_INVALID_ARG;
      }

      // yoffset=#
    } else if (nsCRT::strcmp(token, "yoffset") == 0 && offsetY) {
      if (!value) {
        return NS_ERROR_INVALID_ARG;
      }

      if (PR_sscanf(value, "%u", offsetY) != 1) {
        return NS_ERROR_INVALID_ARG;
      }

      // unknown token name
    } else
      return NS_ERROR_INVALID_ARG;

    if (value) {
      *equals = '=';  // restore '=' so strtok doesn't get lost
    }
  }

#endif
  return NS_OK;
}

NS_IMETHODIMP
nsPNGEncoder::ReadSegments(nsWriteSegmentFun aWriter, void* aClosure,
                           uint32_t aCount, uint32_t* _retval) {
  // Avoid another thread reallocing the buffer underneath us
  ReentrantMonitorAutoEnter autoEnter(mReentrantMonitor);
  return ImageEncoder::ReadSegments(aWriter, aClosure, aCount, _retval);
}

// nsPNGEncoder::WarningCallback

void nsPNGEncoder::WarningCallback(png_structp png_ptr,
                                   png_const_charp warning_msg) {
  MOZ_LOG(sPNGEncoderLog, LogLevel::Warning,
          ("libpng warning: %s\n", warning_msg));
}

// nsPNGEncoder::ErrorCallback

void nsPNGEncoder::ErrorCallback(png_structp png_ptr,
                                 png_const_charp error_msg) {
  MOZ_LOG(sPNGEncoderLog, LogLevel::Error, ("libpng error: %s\n", error_msg));
  png_longjmp(png_ptr, 1);
}

// nsPNGEncoder::WriteCallback

void  // static
nsPNGEncoder::WriteCallback(png_structp png, png_bytep data, png_size_t size) {
  nsPNGEncoder* that = static_cast<nsPNGEncoder*>(png_get_io_ptr(png));
  if (!that->HasBuffer()) {
    return;
  }

  CheckedUint32 sizeNeeded = CheckedUint32(that->BufferSizeWritten()) + size;
  if (!sizeNeeded.isValid()) {
    // Take the lock to ensure that nobody is trying to read from the buffer
    // we are destroying
    ReentrantMonitorAutoEnter autoEnter(that->mReentrantMonitor);

    that->ReleaseBuffer();
    return;
  }

  if (sizeNeeded.value() > that->BufferSize()) {
    // When we're reallocing the buffer we need to take the lock to ensure
    // that nobody is trying to read from the buffer we are destroying
    ReentrantMonitorAutoEnter autoEnter(that->mReentrantMonitor);

    while (sizeNeeded.value() > that->BufferSize()) {
      // expand buffer, just double each time
      CheckedUint32 bufferSize = CheckedUint32(that->BufferSize()) * 2;
      if (!bufferSize.isValid()) {
        that->ReleaseBuffer();
        return;
      }
      nsresult rv = that->ReallocateBuffer(bufferSize.value());
      if (NS_FAILED(rv)) {
        // can't resize, just zero (this will keep us from writing more)
        that->ReleaseBuffer();
        return;
      }
    }
  }

  that->WriteBuffer(data, size);
  that->NotifyListener();
}

void nsPNGEncoder::NotifyListener() {
  // We might call this function on multiple threads (any threads that call
  // AsyncWait and any that do encoding) so we lock to avoid notifying the
  // listener twice about the same data (which generally leads to a truncated
  // image).
  ReentrantMonitorAutoEnter autoEnter(mReentrantMonitor);
  ImageEncoder::NotifyListener();
}
