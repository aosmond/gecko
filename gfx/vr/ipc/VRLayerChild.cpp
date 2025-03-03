/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRLayerChild.h"
#include "gfxPlatform.h"
#include "GLScreenBuffer.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "SharedSurface.h"                // for SharedSurface
#include "SharedSurfaceGL.h"              // for SharedSurface
#include "mozilla/layers/LayersMessages.h" // for TimedTexture
#include "nsICanvasRenderingContextInternal.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/layers/SyncObject.h" // for SyncObjectClient

namespace mozilla {
namespace gfx {

VRLayerChild::VRLayerChild()
  : mCanvasElement(nullptr)
  , mIPCOpen(false)
  , mLastSubmittedFrameId(0)
{
  MOZ_COUNT_CTOR(VRLayerChild);
}

VRLayerChild::~VRLayerChild()
{
  ClearSurfaces();

  MOZ_COUNT_DTOR(VRLayerChild);
}

void
VRLayerChild::Initialize(dom::HTMLCanvasElement* aCanvasElement,
                         const gfx::Rect& aLeftEyeRect, const gfx::Rect& aRightEyeRect)
{
  MOZ_ASSERT(aCanvasElement);
  mLeftEyeRect = aLeftEyeRect;
  mRightEyeRect = aRightEyeRect;
  if (mCanvasElement == nullptr) {
    mCanvasElement = aCanvasElement;
    VRManagerChild *vrmc = VRManagerChild::Get();
    vrmc->RunFrameRequestCallbacks();
  } else {
    mCanvasElement = aCanvasElement;
  }
}

void
VRLayerChild::SubmitFrame(uint64_t aFrameId)
{
  // aFrameId will not increment unless the previuosly submitted
  // frame was received by the VR thread and submitted to the VR
  // compositor.  We early-exit here in the event that SubmitFrame
  // was called twice for the same aFrameId.
  if (!mCanvasElement || aFrameId == mLastSubmittedFrameId) {
    return;
  }
  mLastSubmittedFrameId = aFrameId;

  // Keep the SharedSurfaceTextureClient alive long enough for
  // 1 extra frame, accomodating overlapped asynchronous rendering.
  mLastFrameTexture = mThisFrameTexture;

  mThisFrameTexture = mCanvasElement->GetVRFrame();
  if (!mThisFrameTexture) {
    return;
  }
  VRManagerChild* vrmc = VRManagerChild::Get();
  layers::SyncObjectClient* syncObject = vrmc->GetSyncObject();
  mThisFrameTexture->SyncWithObject(syncObject);
  if (!gfxPlatform::GetPlatform()->DidRenderingDeviceReset()) {
    if (syncObject && syncObject->IsSyncObjectValid()) {
      syncObject->Synchronize();
    }
  }

  gl::SharedSurface* surf = mThisFrameTexture->Surf();
  if (surf->mType == gl::SharedSurfaceType::Basic) {
    gfxCriticalError() << "SharedSurfaceType::Basic not supported for WebVR";
    return;
  }

  layers::SurfaceDescriptor desc;
  if (!surf->ToSurfaceDescriptor(&desc)) {
    gfxCriticalError() << "SharedSurface::ToSurfaceDescriptor failed in VRLayerChild::SubmitFrame";
    return;
  }

  SendSubmitFrame(desc, aFrameId, mLeftEyeRect, mRightEyeRect);
}

bool
VRLayerChild::IsIPCOpen()
{
  return mIPCOpen;
}

void
VRLayerChild::ClearSurfaces()
{
  mThisFrameTexture = nullptr;
  mLastFrameTexture = nullptr;
}

void
VRLayerChild::ActorDestroy(ActorDestroyReason aWhy)
{
  mIPCOpen = false;
}

// static
PVRLayerChild*
VRLayerChild::CreateIPDLActor()
{
  VRLayerChild* c = new VRLayerChild();
  c->AddIPDLReference();
  return c;
}

// static
bool
VRLayerChild::DestroyIPDLActor(PVRLayerChild* actor)
{
  static_cast<VRLayerChild*>(actor)->ReleaseIPDLReference();
  return true;
}

void
VRLayerChild::AddIPDLReference() {
  MOZ_ASSERT(mIPCOpen == false);
  mIPCOpen = true;
  AddRef();
}
void
VRLayerChild::ReleaseIPDLReference() {
  MOZ_ASSERT(mIPCOpen == false);
  Release();
}

} // namespace gfx
} // namespace mozilla
