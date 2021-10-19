/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_OFFSCREENCANVASLISTENER_H_
#define MOZILLA_DOM_OFFSCREENCANVASLISTENER_H_

#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "nsISupportsImpl.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace dom {
class HTMLCanvasElement;

class OffscreenCanvasListener final : public Runnable {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OffscreenCanvasListener)

 public:
  OffscreenCanvasListener(HTMLCanvasElement* aCanvasElement);

  bool Invalidate(layers::SurfaceDescriptor&& aFrontBufferDesc);
  layers::SurfaceDescriptor GetFrontBufferDesc() const;

  NS_IMETHOD Run() final {
    InvalidateElement();
    return NS_OK;
  }

  void Destroy();

 private:
  ~OffscreenCanvasListener() = default;
  void InvalidateElement();

  mutable Mutex mMutex;
  HTMLCanvasElement* MOZ_NON_OWNING_REF mCanvasElement;
  layers::SurfaceDescriptor mFrontBufferDesc;
  bool mPendingInvalidate;
};

}  // namespace dom
}  // namespace mozilla

#endif  // MOZILLA_DOM_OFFSCREENCANVASLISTENER_H_
