/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasDisplayHelper.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/TextureWrapperImage.h"
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
    HTMLCanvasElement* aCanvasElement, uint32_t aWidth, uint32_t aHeight)
    : mMutex("mozilla::dom::OffscreenCanvasDisplayHelper"),
      mCanvasElement(aCanvasElement),
      mWidth(aWidth),
      mHeight(aHeight),
      mImageProducerID(layers::ImageContainer::AllocateProducerID()) {}

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

CanvasContextType OffscreenCanvasDisplayHelper::GetContextType() const {
  MutexAutoLock lock(mMutex);
  return mType;
}

already_AddRefed<layers::ImageContainer>
OffscreenCanvasDisplayHelper::GetImageContainer() const {
  MutexAutoLock lock(mMutex);
  RefPtr<layers::ImageContainer> container = mImageContainer;
  return container.forget();
}

void OffscreenCanvasDisplayHelper::UpdateContext(
    layers::ImageContainer* aContainer, CanvasContextType aType) {
  MutexAutoLock lock(mMutex);
  mImageContainer = aContainer;
  mType = aType;

  if (!mPendingInvalidate) {
    nsCOMPtr<nsIRunnable> self(this);
    NS_DispatchToMainThread(self.forget());
    mPendingInvalidate = true;
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
    nsICanvasRenderingContextInternal* aContext,
    layers::TextureType aTextureType) {
  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;
  layers::TextureFlags flags = TextureFlags::NO_FLAGS;

  {
    MutexAutoLock lock(mMutex);
    if (!mCanvasElement) {
      // Our weak reference to the canvas element has been cleared, so we cannot
      // present directly anymore.
      return false;
    }

    if (!mHasAlpha) {
      format = gfx::SurfaceFormat::B8G8R8X8;
    } else if (!mIsPremultiplied) {
      flags |= TextureFlags::NON_PREMULTIPLIED;
    }
  }

  auto imageBridge = layers::ImageBridgeChild::GetSingleton();
  if (!imageBridge) {
    return false;
  }

  RefPtr<layers::Image> image;
  RefPtr<gfx::SourceSurface> surface;

  Maybe<layers::SurfaceDescriptor> desc =
      aContext->PresentFrontBuffer(nullptr, aTextureType);
  if (desc) {
    RefPtr<layers::TextureClient> texture =
        layers::SharedSurfaceTextureData::CreateTextureClient(
            *desc, format, gfx::IntSize(mWidth, mHeight), flags, imageBridge);
    if (texture) {
      image = new layers::TextureWrapperImage(
          texture, gfx::IntRect(0, 0, mWidth, mHeight));
    }
  } else {
    surface = aContext->GetFrontBufferSnapshot(/* requireAlphaPremult */ true);
    if (surface) {
      image = new layers::SourceSurfaceImage(surface);
    }
  }

  // Image containers are thread safe, so we don't need to lock to update that.
  AutoTArray<layers::ImageContainer::NonOwningImage, 1> imageList;
  imageList.AppendElement(layers::ImageContainer::NonOwningImage(
      image, TimeStamp(), mLastFrameID++, mImageProducerID));
  mImageContainer->SetCurrentImages(imageList);
  printf_stderr("[AO] SetCurrentImages\n");

  MutexAutoLock lock(mMutex);
  mFrontBufferDesc = std::move(desc);
  mFrontBufferSurface = std::move(surface);
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
  return mFrontBufferDesc;
}

already_AddRefed<gfx::SourceSurface>
OffscreenCanvasDisplayHelper::GetSurfaceSnapshot(gfxAlphaType* out_alphaType) {
  MOZ_ASSERT(NS_IsMainThread());

  gfx::SurfaceFormat format = gfx::SurfaceFormat::B8G8R8A8;
  layers::TextureFlags flags = TextureFlags::NO_FLAGS;
  Maybe<layers::SurfaceDescriptor> desc;

  {
    MutexAutoLock lock(mMutex);
    if (mFrontBufferSurface) {
      RefPtr<gfx::SourceSurface> surface = mFrontBufferSurface;
      return surface.forget();
    }

    desc = mFrontBufferDesc;
    if (!mHasAlpha) {
      format = gfx::SurfaceFormat::B8G8R8X8;
    } else if (!mIsPremultiplied) {
      flags |= TextureFlags::NON_PREMULTIPLIED;
    }
  }

  if (!desc) {
    return nullptr;
  }

  auto imageBridge = layers::ImageBridgeChild::GetSingleton();
  if (!imageBridge) {
    return nullptr;
  }

  RefPtr<layers::TextureClient> texture =
      layers::SharedSurfaceTextureData::CreateTextureClient(
          *desc, format, gfx::IntSize(mWidth, mHeight), flags, imageBridge);
  if (!texture) {
    return nullptr;
  }

  return texture->BorrowSnapshot();
}

bool OffscreenCanvasDisplayHelper::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  // FIXME(aosmond): What about dimension size change in the OffscreenCanvas?
  CanvasRenderer* renderer = aCanvasData->GetCanvasRenderer();
  if (renderer) {
    MutexAutoLock lock(mMutex);
    if (!mPendingUpdateParameters) {
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

  layers::CanvasRendererData data;
  data.mDisplay = this;
  data.mOriginPos = gl::OriginPos::BottomLeft;
  data.mIsOpaque = !mHasAlpha;
  data.mIsAlphaPremult = !mHasAlpha || mIsPremultiplied;
  data.mSize = {mWidth, mHeight};
  data.mDoPaintCallbacks = false;

  aRenderer->Initialize(data);
  aRenderer->SetDirty();
  mPendingUpdateParameters = false;
  return true;
}

}  // namespace mozilla::dom
