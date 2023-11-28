/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_IPC_SOURCESURFACEDESCRIPTOR_H_
#define MOZILLA_LAYERS_IPC_SOURCESURFACEDESCRIPTOR_H_

#include "mozilla/gfx/2D.h"
#include "mozilla/layers/LayersSurfaces.h"

namespace mozilla::layers {

class Image;

/**
 * This class is used to wrap SurfaceDescriptor objects.
 */
class SourceSurfaceDescriptor final : public gfx::SourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceDescriptor, override)

  static already_AddRefed<SourceSurfaceDescriptor> CreateForCanvas(
      Image* aImage);

  gfx::SurfaceType GetType() const override {
    return gfx::SurfaceType::DESCRIPTOR;
  }
  gfx::IntSize GetSize() const override { return mSize; }
  gfx::SurfaceFormat GetFormat() const override { return mFormat; }
  already_AddRefed<gfx::DataSourceSurface> GetDataSurface() override;

  const SurfaceDescriptor& GetDesc() const { return mDesc; }

 private:
  SourceSurfaceDescriptor(Image* aImage, const SurfaceDescriptor& aDesc,
                          const gfx::IntSize& aSize,
                          gfx::SurfaceFormat aFormat);
  ~SourceSurfaceDescriptor() override;

  RefPtr<Image> mImage;
  SurfaceDescriptor mDesc;
  gfx::IntSize mSize;
  gfx::SurfaceFormat mFormat;
};

}  // namespace mozilla::layers

#endif /* MOZILLA_LAYERS_IPC_SOURCESURFACEDESCRIPTOR_H_ */
