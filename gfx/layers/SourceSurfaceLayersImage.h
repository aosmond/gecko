/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACELAYERSIMAGE_H_
#define MOZILLA_GFX_SOURCESURFACELAYERSIMAGE_H_

#include "mozilla/gfx/2D.h"

namespace mozilla {
namespace layers {
class Image;
}

namespace gfx {

/**
 * This class is used to wrap layers::Image objects.
 */
class SourceSurfaceLayersImage final : public SourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceLayersImage, override)

  explicit SourceSurfaceLayersImage(RefPtr<layers::Image>&& aLayersImage);

  SurfaceType GetType() const override { return SurfaceType::LAYERS_IMAGE; }

  IntSize GetSize() const override { return mSize; }

  SurfaceFormat GetFormat() const override { return mFormat; }

  already_AddRefed<DataSourceSurface> GetDataSurface() override;

  const RefPtr<layers::Image>& GetLayersImage() const { return mLayersImage; }

 private:
  RefPtr<layers::Image> mLayersImage;
  IntSize mSize;
  SurfaceFormat mFormat;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACELAYERSIMAGE_H_ */
