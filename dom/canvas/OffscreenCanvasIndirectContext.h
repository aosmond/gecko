/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_OFFSCREENCANVASINDIRECTCONTEXT_H_
#define MOZILLA_DOM_OFFSCREENCANVASINDIRECTCONTEXT_H_

#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsISupportsImpl.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace dom {
class HTMLCanvasElement;

class OffscreenCanvasIndirectContext final
    : public nsICanvasRenderingContextDisplay,
      public nsIRunnable,
      public nsINamed {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSINAMED

 public:
  explicit OffscreenCanvasIndirectContext(HTMLCanvasElement* aCanvasElement);

  bool Invalidate(layers::SurfaceDescriptor&& aFrontBufferDesc);
  layers::SurfaceDescriptor GetFrontBufferDesc() const;

  void Destroy();

  mozilla::UniquePtr<uint8_t[]> GetImageBuffer(int32_t* format) override;

  NS_IMETHOD GetInputStream(const char* mimeType,
                            const nsAString& encoderOptions,
                            nsIInputStream** stream) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType = nullptr) override;

 private:
  ~OffscreenCanvasIndirectContext();
  void InvalidateElement();

  mutable Mutex mMutex;
  HTMLCanvasElement* MOZ_NON_OWNING_REF mCanvasElement;
  layers::SurfaceDescriptor mFrontBufferDesc;
  bool mPendingInvalidate;
};

}  // namespace dom
}  // namespace mozilla

#endif  // MOZILLA_DOM_OFFSCREENCANVASINDIRECTCONTEXT_H_
