/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/SourceSurfaceDescriptor.h"
#include "ImageContainer.h"

namespace mozilla::layers {

SourceSurfaceDescriptor::SourceSurfaceDescriptor(Image* aImage,
                                                 const SurfaceDescriptor& aDesc,
                                                 const gfx::IntSize& aSize,
                                                 gfx::SurfaceFormat aFormat)
    : mImage(aImage), mDesc(aDesc), mSize(aSize), mFormat(aFormat) {}

SourceSurfaceDescriptor::~SourceSurfaceDescriptor() = default;

already_AddRefed<gfx::DataSourceSurface>
SourceSurfaceDescriptor::GetDataSurface() {
  RefPtr<gfx::SourceSurface> surface = mImage->GetAsSourceSurface();
  if (!surface) {
    return nullptr;
  }
  return surface->GetDataSurface();
}

/* static */ already_AddRefed<SourceSurfaceDescriptor>
SourceSurfaceDescriptor::CreateForCanvas(Image* aImage) {
  if (!aImage) {
    return nullptr;
  }

  switch (aImage->GetFormat()) {
#ifdef XP_WIN
    case ImageFormat::D3D11_SHARE_HANDLE_TEXTURE:
    case ImageFormat::D3D11_TEXTURE_IMF_SAMPLE:
    case ImageFormat::D3D11_YCBCR_IMAGE:
#endif
    case ImageFormat::GPU_VIDEO:
      break;
    default:
      return nullptr;
  }

  Maybe<SurfaceDescriptor> desc = aImage->GetDesc();
  if (!desc) {
    return nullptr;
  }

  RefPtr<SourceSurfaceDescriptor> surface = new SourceSurfaceDescriptor(
      aImage, desc.ref(), aImage->GetSize(), gfx::SurfaceFormat::UNKNOWN);
  return surface.forget();
}

}  // namespace mozilla::layers
