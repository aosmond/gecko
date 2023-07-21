/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageTypes_h
#define mozilla_image_ImageTypes_h

namespace mozilla::image {

/**
 * The type of decoder; this is usually determined from a MIME type using
 * DecoderFactory::GetDecoderType() or ImageUtils::GetDecoderType().
 */
enum class DecoderType {
  PNG,
  GIF,
  JPEG,
  BMP,
  BMP_CLIPBOARD,
  ICO,
  ICON,
  WEBP,
  AVIF,
  JXL,
  UNKNOWN
};

}  // namespace mozilla::image

#endif  // mozilla_image_ImageUtils_h
