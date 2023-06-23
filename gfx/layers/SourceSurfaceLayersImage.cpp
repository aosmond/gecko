/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/SourceSurfaceLayersImage.h"
#include "ImageContainer.h"

namespace mozilla::gfx {

SourceSurfaceLayersImage::SourceSurfaceLayersImage(
    RefPtr<layers::Image>&& aLayersImage)
    : mLayersImage(std::move(aLayersImage)),
      mSize(aLayersImage->GetSize()),
      mFormat(SurfaceFormat::B8G8R8A8) {}

already_AddRefed<DataSourceSurface> SourceSurfaceLayersImage::GetDataSurface() {
  RefPtr<SourceSurface> surface = mLayersImage->GetAsSourceSurface();
  if (!surface) {
    return nullptr;
  }
  return surface->GetDataSurface();
}

}  // namespace mozilla::gfx
