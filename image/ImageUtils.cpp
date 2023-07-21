/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/image/ImageUtils.h"
#include "mozilla/gfx/2D.h"

namespace mozilla::image {

DecodeImageResult::DecodeImageResult() = default;

DecodeImageResult::~DecodeImageResult() = default;

/* static */ already_AddRefed<DecodeImagePromise>
ImageUtils::DecodeAnonymousImage(const nsACString& aMimeType,
                                 ErrorResult& aRv) {
  return nullptr;
}

/* static */ DecoderType ImageUtils::GetDecoderType(
    const nsACString& aMimeType) {
  return DecoderFactory::GetDecoderType(aMimeType.Data());
}

}  // namespace mozilla::image
