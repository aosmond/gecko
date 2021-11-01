/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
#define MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_

#include "mozilla/Maybe.h"
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

  bool UpdateParameters(uint32_t aWidth, uint32_t aHeight, bool aHasAlpha,
                        bool aIsPremultiplied);

  bool Invalidate(Maybe<layers::SurfaceDescriptor>&& aFrontBufferDesc);

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
  void DispatchEvent();
  void InvalidateElement();

  mutable Mutex mMutex;
  HTMLCanvasElement* MOZ_NON_OWNING_REF mCanvasElement;
  Maybe<layers::SurfaceDescriptor> mFrontBufferDesc;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  bool mPendingInvalidate = false;
  bool mPendingUpdateParameters = false;
  bool mHasAlpha = false;
  bool mIsPremultiplied = false;
};

}  // namespace mozilla::dom

/**
 * Casting OffscreenCanvasDisplayHelper to nsISupports is ambiguous.
 * This method handles that.
 */
inline nsISupports* ToSupports(mozilla::dom::OffscreenCanvasDisplayHelper* p) {
  return NS_ISUPPORTS_CAST(nsICanvasRenderingDisplay*, p);
}

#endif  // MOZILLA_DOM_OFFSCREENCANVASDISPLAYHELPER_H_
