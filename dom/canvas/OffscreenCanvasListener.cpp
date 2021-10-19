/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasListener.h"
#include "mozilla/SVGObserverUtils.h"

namespace mozilla {
namespace dom {

OffscreenCanvasListener::OffscreenCanvasListener(
    HTMLCanvasElement* aCanvasElement)
    : Runnable("mozilla::dom::OffscreenCanvasListener"),
      mMutex("mozilla::dom::OffscreenCanvasListener"),
      mCanvasElement(aCanvasElement) {}

void OffscreenCanvasListener::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  mCanvasElement = nullptr;
}

layers::SurfaceDescriptor OffscreenCanvasListener::GetFrontBufferDesc() const {
  MutexAutoLock lock(mMutex);
  return mFrontBufferDesc;
}

bool OffscreenCanvasListener::Invalidate(
    layers::SurfaceDescriptor&& aFrontBufferDesc) {
  MutexAutoLock lock(mMutex);
  if (!mCanvasElement) {
    // Our weak reference to the canvas element has been cleared, so we cannot
    // present directly anymore.
    return false;
  }

  mFrontBufferDesc = std::move(aFrontBufferDesc);
  if (!mPendingInvalidate) {
    nsCOMPtr<nsIRunnable> self(this);
    NS_DispatchToMainThread(self.forget());
    mPendingInvalidate = true;
  }
  return true;
}

void OffscreenCanvasListener::InvalidateElement() {
  MOZ_ASSERT(NS_IsMainThread());

  HTMLCanvasElement* canvasElement;

  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mPendingInvalidate);
    mPendingInvalidate = false;
    canvasElement = mCanvasElement;
  }

  if (canvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(canvasElement);
    canvasElement->InvalidateCanvasContent(nullptr);
  }
}

}  // namespace dom
}  // namespace mozilla
