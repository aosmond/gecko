/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
#define MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_

#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsISupportsImpl.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {
class HTMLCanvasElement;

class OffscreenCanvasDisplayHelper final : public nsICanvasRenderingDisplay,
                                           public nsIRunnable,
                                           public nsINamed {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSINAMED

 public:
  explicit OffscreenCanvasDisplayHelper(HTMLCanvasElement* aCanvasElement);

  bool Invalidate(layers::SurfaceDescriptor&& aFrontBufferDesc);

  void Destroy();

  mozilla::Maybe<mozilla::layers::SurfaceDescriptor> GetFrontBuffer(
      mozilla::WebGLFramebufferJS*, const bool webvr = false) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType = nullptr) override;

  bool UpdateWebRenderCanvasData(mozilla::nsDisplayListBuilder* aBuilder,
                                 WebRenderCanvasData* aCanvasData) override;

  bool InitializeCanvasRenderer(mozilla::nsDisplayListBuilder* aBuilder,
                                CanvasRenderer* aRenderer) override;

 private:
  ~OffscreenCanvasDisplayHelper();
  void InvalidateElement();

  mutable Mutex mMutex;
  HTMLCanvasElement* MOZ_NON_OWNING_REF mCanvasElement;
  layers::SurfaceDescriptor mFrontBufferDesc;
  bool mPendingInvalidate;
};

}  // namespace mozilla::dom

#endif  // MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
