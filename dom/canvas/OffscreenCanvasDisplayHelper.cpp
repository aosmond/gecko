/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasDisplayHelper.h"
#include "mozilla/SVGObserverUtils.h"

namespace mozilla::dom {

NS_IMPL_ADDREF(OffscreenCanvasDisplayHelper)
NS_IMPL_RELEASE(OffscreenCanvasDisplayHelper)

NS_INTERFACE_MAP_BEGIN(OffscreenCanvasDisplayHelper)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICanvasRenderingDisplay)
  NS_INTERFACE_MAP_ENTRY(nsICanvasRenderingDisplay)
  NS_INTERFACE_MAP_ENTRY(nsIRunnable)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
NS_INTERFACE_MAP_END

OffscreenCanvasDisplayHelper::OffscreenCanvasDisplayHelper(
    HTMLCanvasElement* aCanvasElement)
    : mMutex("mozilla::dom::OffscreenCanvasDisplayHelper"),
      mCanvasElement(aCanvasElement) {}

OffscreenCanvasDisplayHelper::~OffscreenCanvasDisplayHelper() = default;

NS_IMETHODIMP OffscreenCanvasDisplayHelper::Run() {
  printf_stderr("[AO][%p] Run\n", this);
  if (mPendingInvalidate) {
    InvalidateElement();
  }
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

void OffscreenCanvasDisplayHelper::DispatchEvent() {
  if (!mPendingInvalidate) {
    printf_stderr("[AO][%p] DispatchEvent\n", this);
    nsCOMPtr<nsIRunnable> self(this);
    NS_DispatchToMainThread(self.forget());
  }
}

bool OffscreenCanvasDisplayHelper::UpdateParameters(uint32_t aWidth,
                                                    uint32_t aHeight,
                                                    bool aHasAlpha,
                                                    bool aIsPremultiplied) {
  MutexAutoLock lock(mMutex);
  if (!mCanvasElement) {
    // Our weak reference to the canvas element has been cleared, so we cannot
    // present directly anymore.
    return false;
  }

  mWidth = aWidth;
  mHeight = aHeight;
  mHasAlpha = aHasAlpha;
  mIsPremultiplied = aIsPremultiplied;
  mPendingUpdateParameters = true;
  return true;
}

bool OffscreenCanvasDisplayHelper::Invalidate(
    Maybe<layers::SurfaceDescriptor>&& aFrontBufferDesc) {
  MutexAutoLock lock(mMutex);
  if (!mCanvasElement) {
    // Our weak reference to the canvas element has been cleared, so we cannot
    // present directly anymore.
    printf_stderr("[AO][%p] Invalidate failed\n", this);
    return false;
  }

  mFrontBufferDesc = std::move(aFrontBufferDesc);

  printf_stderr("[AO][%p] Invalidate queued\n", this);
  DispatchEvent();
  mPendingInvalidate = true;
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

  printf_stderr("[AO][%p] InvalidateElement -- %p\n", this, canvasElement);
  if (canvasElement) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(canvasElement);
    canvasElement->InvalidateCanvasContent(nullptr);
  }
}

Maybe<layers::SurfaceDescriptor> OffscreenCanvasDisplayHelper::GetFrontBuffer(
    WebGLFramebufferJS*, const bool webvr) {
  MutexAutoLock lock(mMutex);
  printf_stderr("[AO][%p] GetFrontBuffer -- %d\n", this,
                mFrontBufferDesc.isSome());
  return mFrontBufferDesc;
}

already_AddRefed<gfx::SourceSurface>
OffscreenCanvasDisplayHelper::GetSurfaceSnapshot(gfxAlphaType* out_alphaType) {
  return nullptr;
}

bool OffscreenCanvasDisplayHelper::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  // FIXME(aosmond): What about dimension size change in the OffscreenCanvas?
  CanvasRenderer* renderer = aCanvasData->GetCanvasRenderer();
  if (renderer) {
    MutexAutoLock lock(mMutex);
    if (!mPendingUpdateParameters) {
      printf_stderr("[AO][%p] UpdateWebRenderCanvasData -- reuse %p\n", this,
                    renderer);
      return true;
    }
  }

  renderer = aCanvasData->CreateCanvasRenderer();
  if (!InitializeCanvasRenderer(aBuilder, renderer)) {
    aCanvasData->ClearCanvasRenderer();
    return false;
  }

  return true;
}

bool OffscreenCanvasDisplayHelper::InitializeCanvasRenderer(
    nsDisplayListBuilder* aBuilder, CanvasRenderer* aRenderer) {
  MutexAutoLock lock(mMutex);
  // FIXME(aosmond): Check for context lost?

  printf_stderr("[AO][%p] Initialize -- %p (%dx%d)\n", this, aRenderer, mWidth,
                mHeight);
  layers::CanvasRendererData data;
  data.mDisplay = this;
  data.mOriginPos = gl::OriginPos::BottomLeft;
  data.mIsOpaque = !mHasAlpha;
  data.mIsAlphaPremult = !mHasAlpha || mIsPremultiplied;
  data.mSize = {mWidth, mHeight};
  if (aBuilder->IsPaintingToWindow() && mCanvasElement) {
    data.mDoPaintCallbacks = true;
  }

  aRenderer->Initialize(data);
  aRenderer->SetDirty();
  mPendingUpdateParameters = false;
  return true;
}

}  // namespace mozilla::dom
