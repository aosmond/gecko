/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasManagerParent.h"
#include "gfxPlatform.h"
#include "mozilla/dom/WebGLParent.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CanvasTranslator.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

namespace mozilla::gfx {

CanvasManagerParent::ManagerSet CanvasManagerParent::sManagers;

/* static */ void CanvasManagerParent::Init(
    Endpoint<PCanvasManagerParent>&& aEndpoint,
    layers::SharedSurfacesHolder* aSharedSurfacesHolder,
    const dom::ContentParentId& aContentId) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());

  auto manager =
      MakeRefPtr<CanvasManagerParent>(aSharedSurfacesHolder, aContentId);

  nsCOMPtr<nsIThread> owningThread =
      gfx::CanvasRenderThread::GetCanvasRenderThread();
  MOZ_ASSERT(owningThread);

  owningThread->Dispatch(NewRunnableMethod<Endpoint<PCanvasManagerParent>&&>(
      "CanvasManagerParent::Bind", manager, &CanvasManagerParent::Bind,
      std::move(aEndpoint)));
}

/* static */ void CanvasManagerParent::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIThread> owningThread =
      gfx::CanvasRenderThread::GetCanvasRenderThread();
  MOZ_ASSERT(owningThread);

  NS_DispatchAndSpinEventLoopUntilComplete(
      "CanvasManagerParent::Shutdown"_ns, owningThread,
      NS_NewRunnableFunction("CanvasManagerParent::Shutdown", []() -> void {
        CanvasManagerParent::ShutdownInternal();
      }));
}

/* static */ void CanvasManagerParent::ShutdownInternal() {
  nsTArray<RefPtr<CanvasManagerParent>> actors(sManagers.Count());
  // Do a copy since Close will remove the entry from the set.
  for (const auto& actor : sManagers) {
    actors.AppendElement(actor);
  }

  for (auto const& actor : actors) {
    actor->Close();
  }
}

/* static */ void CanvasManagerParent::DisableRemoteCanvas() {
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("CanvasManagerParent::DisableRemoteCanvas", [] {
        if (XRE_IsGPUProcess()) {
          GPUParent::GetSingleton()->NotifyDisableRemoteCanvas();
        } else {
          gfxPlatform::DisableRemoteCanvas();
        }
      }));

  if (CanvasRenderThread::IsInCanvasRenderThread()) {
    DisableRemoteCanvasInternal();
    return;
  }

  CanvasRenderThread::Dispatch(NS_NewRunnableFunction(
      "CanvasManagerParent::DisableRemoteCanvas",
      [] { CanvasManagerParent::DisableRemoteCanvasInternal(); }));
}

/* static */ void CanvasManagerParent::DisableRemoteCanvasInternal() {
  MOZ_ASSERT(CanvasRenderThread::IsInCanvasRenderThread());

  AutoTArray<RefPtr<layers::CanvasTranslator>, 16> actors;
  for (const auto& manager : sManagers) {
    for (const auto& canvas : manager->ManagedPCanvasParent()) {
      actors.AppendElement(static_cast<layers::CanvasTranslator*>(canvas));
    }
  }

  for (const auto& actor : actors) {
    Unused << NS_WARN_IF(!actor->SendDeactivate());
  }
}

CanvasManagerParent::CanvasManagerParent(
    layers::SharedSurfacesHolder* aSharedSurfacesHolder,
    const dom::ContentParentId& aContentId)
    : mSharedSurfacesHolder(aSharedSurfacesHolder), mContentId(aContentId) {}

CanvasManagerParent::~CanvasManagerParent() = default;

void CanvasManagerParent::Bind(Endpoint<PCanvasManagerParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    NS_WARNING("Failed to bind CanvasManagerParent!");
    return;
  }

#ifdef DEBUG
  for (CanvasManagerParent* i : sManagers) {
    MOZ_ASSERT_IF(i->mContentId == mContentId,
                  i->OtherPidMaybeInvalid() == OtherPidMaybeInvalid());
  }
#endif

  sManagers.Insert(this);
}

void CanvasManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  sManagers.Remove(this);
}

already_AddRefed<dom::PWebGLParent> CanvasManagerParent::AllocPWebGLParent() {
  return MakeAndAddRef<dom::WebGLParent>(mContentId);
}

already_AddRefed<webgpu::PWebGPUParent>
CanvasManagerParent::AllocPWebGPUParent() {
  if (!gfxVars::AllowWebGPU()) {
    return nullptr;
  }

  return MakeAndAddRef<webgpu::WebGPUParent>();
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvInitialize(
    const uint32_t& aId) {
  if (!aId) {
    return IPC_FAIL(this, "invalid id");
  }
  if (mId) {
    return IPC_FAIL(this, "already initialized");
  }
  mId = aId;
  return IPC_OK();
}

already_AddRefed<layers::PCanvasParent>
CanvasManagerParent::AllocPCanvasParent() {
  MOZ_RELEASE_ASSERT(mId != 0);
  return MakeAndAddRef<layers::CanvasTranslator>(mSharedSurfacesHolder,
                                                 mContentId, mId);
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvGetSnapshot(
    const uint32_t& aManagerId, const int32_t& aProtocolId,
    const Maybe<RemoteTextureOwnerId>& aOwnerId,
    const Maybe<RemoteTextureId>& aTextureId,
    layers::SurfaceDescriptorShared* aDesc) {
  if (NS_WARN_IF(!aManagerId)) {
    return IPC_FAIL(this, "invalid id");
  }

  IProtocol* actor = nullptr;
  for (CanvasManagerParent* i : sManagers) {
    if (i->mContentId == mContentId && i->mId == aManagerId) {
      actor = i->Lookup(aProtocolId);
      break;
    }
  }

  if (NS_WARN_IF(!actor)) {
    return IPC_FAIL(this, "invalid actor");
  }

  if (NS_WARN_IF((actor->GetSide() != mozilla::ipc::Side::ParentSide))) {
    return IPC_FAIL(this, "unsupported actor");
  }

  // In an ideal world, all of the protocols could just read the snapshot
  // from the remote texture present queue. Unfortunately not all of the
  // TextureHosts backing it can easily be read back, so we have to manually
  // readback from WebGL based canvases.
  switch (actor->GetProtocolId()) {
    case ProtocolId::PWebGLMsgStart: {
      RefPtr<dom::WebGLParent> webgl = static_cast<dom::WebGLParent*>(actor);
      Maybe<uvec2> maybeSize = webgl->GetFrontBufferSnapshot({});
      if (NS_WARN_IF(!maybeSize)) {
        return IPC_OK();
      }

      const auto format = SurfaceFormat::R8G8B8A8;
      IntSize size(maybeSize->x, maybeSize->y);
      int32_t stride =
          layers::ImageDataSerializer::ComputeRGBStride(format, size.width);
      if (stride < 0) {
        return IPC_OK();
      }

      size_t len = size_t(stride) * size.height;
      size_t alignedLen = ipc::SharedMemory::PageAlignedSize(len);
      auto shmem = MakeRefPtr<ipc::SharedMemoryBasic>();
      if (NS_WARN_IF(!shmem->Create(alignedLen)) ||
          NS_WARN_IF(!shmem->Map(alignedLen))) {
        return IPC_OK();
      }

      auto range = Range<uint8_t>{reinterpret_cast<uint8_t*>(shmem->memory()),
                                  alignedLen};
      if (NS_WARN_IF(!webgl->GetFrontBufferSnapshot(Some(range),
                                                    Some(size_t(stride))))) {
        return IPC_OK();
      }

      *aDesc =
          SurfaceDescriptorShared(size, stride, format, shmem->TakeHandle());
    } break;
    case ProtocolId::PCanvasMsgStart: {
      if (NS_WARN_IF(!aOwnerId) || NS_WARN_IF(!aTextureId)) {
        return IPC_OK();
      }
      RemoteTextureMap::Get()->GetLatestBufferSnapshot(*aOwnerId, OtherPid(),
                                                       *aDesc);
    } break;
    case ProtocolId::PWebGPUMsgStart: {
      if (NS_WARN_IF(!aOwnerId)) {
        return IPC_OK();
      }

      RefPtr<layers::CanvasTranslator> translator =
          static_cast<layers::CanvasTranslator*>(actor);
      translator->GetLatestBufferSnapshot(*aOwnerId, *aTextureId, *aDesc);
      return IPC_OK();
    } break;
    default:
      return IPC_FAIL(this, "unsupported protocol");
  }

  return IPC_OK();
}

}  // namespace mozilla::gfx
