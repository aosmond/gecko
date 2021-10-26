/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasIndirectContext.h"
#include "mozilla/SVGObserverUtils.h"

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS(OffscreenCanvasIndirectContext,
                  nsICanvasRenderingContextDisplay, nsIRunnable, nsINamed)

OffscreenCanvasIndirectContext::OffscreenCanvasIndirectContext(
    HTMLCanvasElement* aCanvasElement)
    : mMutex("mozilla::dom::OffscreenCanvasIndirectContext"),
      mCanvasElement(aCanvasElement) {}

OffscreenCanvasIndirectContext::~OffscreenCanvasIndirectContext() = default;

NS_IMETHODIMP OffscreenCanvasIndirectContext::Run() {
  InvalidateElement();
  return NS_OK;
}

NS_IMETHODIMP OffscreenCanvasIndirectContext::GetName(nsACString& aNameOut) {
  aNameOut.AssignLiteral("OffscreenCanvasIndirectContext::InvalidateElement");
  return NS_OK;
}

void OffscreenCanvasIndirectContext::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  mCanvasElement = nullptr;
}

layers::SurfaceDescriptor OffscreenCanvasIndirectContext::GetFrontBufferDesc()
    const {
  MutexAutoLock lock(mMutex);
  return mFrontBufferDesc;
}

bool OffscreenCanvasIndirectContext::Invalidate(
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

void OffscreenCanvasIndirectContext::InvalidateElement() {
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

mozilla::UniquePtr<uint8_t[]> OffscreenCanvasIndirectContext::GetImageBuffer(
    int32_t* format) {
  return nullptr;
}

NS_IMETHODIMP OffscreenCanvasIndirectContext::GetInputStream(
    const char* mimeType, const nsAString& encoderOptions,
    nsIInputStream** stream) {
  return NS_ERROR_FAILURE;
}

already_AddRefed<mozilla::gfx::SourceSurface>
OffscreenCanvasIndirectContext::GetSurfaceSnapshot(
    gfxAlphaType* out_alphaType) {
  return nullptr;
}

}  // namespace dom
}  // namespace mozilla
