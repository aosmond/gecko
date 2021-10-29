/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasDisplayHelper.h"
#include "mozilla/SVGObserverUtils.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(OffscreenCanvasDisplayHelper, nsICanvasRenderingDisplay,
                  nsIRunnable, nsINamed)

OffscreenCanvasDisplayHelper::OffscreenCanvasDisplayHelper(
    HTMLCanvasElement* aCanvasElement)
    : mMutex("mozilla::dom::OffscreenCanvasDisplayHelper"),
      mCanvasElement(aCanvasElement) {}

OffscreenCanvasDisplayHelper::~OffscreenCanvasDisplayHelper() = default;

NS_IMETHODIMP OffscreenCanvasDisplayHelper::Run() {
  InvalidateElement();
  return NS_OK;
}

NS_IMETHODIMP OffscreenCanvasDisplayHelper::GetName(nsACString& aNameOut) {
  aNameOut.AssignLiteral("OffscreenCanvasDisplayHelper::InvalidateElement");
  return NS_OK;
}

void OffscreenCanvasDisplayHelper::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  mCanvasElement = nullptr;
}

bool OffscreenCanvasDisplayHelper::Invalidate(
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

void OffscreenCanvasDisplayHelper::InvalidateElement() {
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

Maybe<layers::SurfaceDescriptor> OffscreenCanvasDisplayHelper::GetFrontBuffer(
    WebGLFramebufferJS*, const bool webvr) {
  MutexAutoLock lock(mMutex);
  return Some(mFrontBufferDesc);
}

already_AddRefed<gfx::SourceSurface>
OffscreenCanvasDisplayHelper::GetSurfaceSnapshot(gfxAlphaType* out_alphaType) {
  return nullptr;
}

bool OffscreenCanvasDisplayHelper::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  return false;
}

bool OffscreenCanvasDisplayHelper::InitializeCanvasRenderer(
    nsDisplayListBuilder* aBuilder, CanvasRenderer* aRenderer) {
  return false;
}

}  // namespace mozilla::dom
