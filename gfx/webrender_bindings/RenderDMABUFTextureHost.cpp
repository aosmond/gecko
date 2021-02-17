/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderDMABUFTextureHost.h"

#include "GLContextEGL.h"
#include "mozilla/gfx/Logging.h"
#include "ScopedGLHelpers.h"

namespace mozilla::wr {

RenderDMABUFTextureHost::RenderDMABUFTextureHost(DMABufSurface* aSurface)
    : mSurface(aSurface) {
  MOZ_COUNT_CTOR_INHERITED(RenderDMABUFTextureHost, RenderTextureHost);
}

RenderDMABUFTextureHost::~RenderDMABUFTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderDMABUFTextureHost, RenderTextureHost);
  DeleteTextureHandle();
}

wr::WrExternalImage RenderDMABUFTextureHost::Lock(
    uint8_t aChannelIndex, gl::GLContext* aGL, wr::ImageRendering aRendering) {
  if (mGL.get() != aGL) {
    if (mGL) {
      // This should not happen. EGLImage is created only in
      // parent process.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return InvalidToWrExternalImage();
    }
    mGL = aGL;
  }

  if (!mGL || !mGL->MakeCurrent()) {
    return InvalidToWrExternalImage();
  }

  bool bindTexture = IsFilterUpdateNecessary(aRendering);

  if (!mSurface->GetTexture(aChannelIndex)) {
    if (!mSurface->CreateTexture(mGL, aChannelIndex)) {
      return InvalidToWrExternalImage();
    }
    bindTexture = true;
  }

  if (bindTexture) {
    // Cache new rendering filter.
    mCachedRendering = aRendering;
    ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0, LOCAL_GL_TEXTURE_2D,
                                 mSurface->GetTexture(aChannelIndex),
                                 aRendering);
  }

  return NativeTextureToWrExternalImage(mSurface->GetTexture(aChannelIndex), 0,
                                        0, mSurface->GetWidth(aChannelIndex),
                                        mSurface->GetHeight(aChannelIndex));
}

void RenderDMABUFTextureHost::Unlock() {}

void RenderDMABUFTextureHost::DeleteTextureHandle() {
  mSurface->ReleaseTextures();
}

void RenderDMABUFTextureHost::ClearCachedResources() {
  DeleteTextureHandle();
  mGL = nullptr;
}

size_t RenderDMABUFTextureHost::GetPlaneCount() const {
  return mSurface->GetBufferPlaneCount();
}

gfx::SurfaceFormat RenderDMABUFTextureHost::GetFormat() const {
  switch (mSurface->GetSurfaceType()) {
    case DMABufSurface::SURFACE_NV12:
      return gfx::SurfaceFormat::NV12;
    case DMABufSurface::SURFACE_YUV420:
      return gfx::SurfaceFormat::YUV;
    case DMABufSurface::SURFACE_RGBA:
    default:
      if (mSurface->GetAsDMABufSurfaceRGBA()->HasAlpha()) {
        return gfx::SurfaceFormat::B8G8R8A8;
      }
      return gfx::SurfaceFormat::B8G8R8X8;
  }
}

gfx::ColorDepth RenderDMABUFTextureHost::GetColorDepth() const {
  return gfx::ColorDepth::COLOR_8;
}

gfx::YUVColorSpace RenderDMABUFTextureHost::GetYUVColorSpace() const {
  return mSurface->GetYUVColorSpace();
}

bool RenderDMABUFTextureHost::MapPlane(RenderCompositor* aCompositor,
                                       uint8_t aChannelIndex,
                                       PlaneInfo& aPlaneInfo) {
  uint32_t stride = 0;
  void* data = mSurface->MapReadOnly(&stride, aChannelIndex);
  if (!data) {
    return false;
  }

  aPlaneInfo.mData = data;
  aPlaneInfo.mSize = gfx::IntSize(mSurface->GetWidth(aChannelIndex),
                                  mSurface->GetHeight(aChannelIndex));
  aPlaneInfo.mStride = stride;
  return true;
}

void RenderDMABUFTextureHost::UnmapPlanes() {
  int planeCount = mSurface->GetBufferPlaneCount();
  for (int i = 0; i < planeCount; ++i) {
    mSurface->Unmap(i);
  }
}

}  // namespace mozilla::wr
