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
#include "mozilla/webgpu/WebGPUParent.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

namespace mozilla::gfx {

StaticMonitor CanvasManagerParent::sManagerMonitor;
CanvasManagerParent::ManagerMap CanvasManagerParent::sManagers;
bool CanvasManagerParent::sReplayTexturesEnabled(true);

/* static */ void CanvasManagerParent::Init(
    Endpoint<PCanvasManagerParent>&& aEndpoint,
    const dom::ContentParentId& aContentId) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());

  auto manager = MakeRefPtr<CanvasManagerParent>(aContentId);

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
  nsTArray<RefPtr<CanvasManagerParent>> actors;

  {
    StaticMonitorAutoLock lock(sManagerMonitor);
    // Do a copy since Close will remove the entry from the set.
    actors.SetCapacity(sManagers.size());
    for (const auto& i : sManagers) {
      actors.AppendElement(i.second);
    }
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

  {
    StaticMonitorAutoLock lock(sManagerMonitor);

    for (const auto& i : sManagers) {
      const auto& manager = i.second;
      manager->mReplayTextures.Clear();

      for (const auto& canvas : manager->ManagedPCanvasParent()) {
        actors.AppendElement(static_cast<layers::CanvasTranslator*>(canvas));
      }
    }

    sReplayTexturesEnabled = false;
  }

  for (const auto& actor : actors) {
    Unused << NS_WARN_IF(!actor->SendDeactivate());
  }

  {
    StaticMonitorAutoLock lock(sManagerMonitor);
    lock.NotifyAll();
  }
}

/* static */ void CanvasManagerParent::AddReplayTexture(
    const dom::ContentParentId& aContentId, int64_t aTextureId,
    layers::TextureData* aTextureData) {
  StaticMonitorAutoLock lock(sManagerMonitor);
  auto i = sManagers.find(uint64_t(aContentId));
  if (NS_WARN_IF(i == sManagers.end())) {
    // If the manager is gone, then we must be on the verge of destroying.
    return;
  }

  auto desc = MakeUnique<layers::SurfaceDescriptor>();
  if (!aTextureData->Serialize(*desc)) {
    MOZ_CRASH("Failed to serialize");
  }

  i->second->mReplayTextures.AppendElement(
      ReplayTexture{aTextureId, std::move(desc)});
  lock.NotifyAll();
}

/* static */ void CanvasManagerParent::RemoveReplayTexture(
    const dom::ContentParentId& aContentId, int64_t aTextureId) {
  StaticMonitorAutoLock lock(sManagerMonitor);
  auto i = sManagers.find(uint64_t(aContentId));
  if (NS_WARN_IF(i == sManagers.end())) {
    // If the manager is gone, then it must already be removed.
    return;
  }

  CanvasManagerParent* manager = i->second;
  auto k = manager->mReplayTextures.Length();
  while (k > 0) {
    --k;
    const auto& texture = manager->mReplayTextures[k];
    if (texture.mId == aTextureId) {
      manager->mReplayTextures.RemoveElementAt(k);
      break;
    }
  }
}

/* static */ void CanvasManagerParent::RemoveReplayTextures(
    const dom::ContentParentId& aContentId) {
  StaticMonitorAutoLock lock(sManagerMonitor);
  auto i = sManagers.find(uint64_t(aContentId));
  if (NS_WARN_IF(i == sManagers.end())) {
    // If the manager is gone, then it must already be removed.
    return;
  }

  i->second->mReplayTextures.Clear();
}

UniquePtr<layers::SurfaceDescriptor> CanvasManagerParent::TakeReplayTexture(
    int64_t aTextureId) {
  // While in theory this could be relatively expensive, the array is most
  // likely very small as the textures are removed during each composite.
  auto i = mReplayTextures.Length();
  while (i > 0) {
    --i;
    const auto& texture = mReplayTextures[i];
    if (texture.mId == aTextureId) {
      UniquePtr<layers::SurfaceDescriptor> desc =
          std::move(mReplayTextures[i].mDesc);
      mReplayTextures.RemoveElementAt(i);
      return desc;
    }
  }
  return nullptr;
}

/* static */ UniquePtr<layers::SurfaceDescriptor>
CanvasManagerParent::WaitForReplayTexture(
    const dom::ContentParentId& aContentId, int64_t aTextureId) {
  MOZ_ASSERT(!CanvasRenderThread::IsInCanvasRenderThread());

  StaticMonitorAutoLock lock(sManagerMonitor);

  auto i = sManagers.find(uint64_t(aContentId));
  if (NS_WARN_IF(i == sManagers.end())) {
    return nullptr;
  }

  RefPtr<CanvasManagerParent> manager = i->second;

  UniquePtr<layers::SurfaceDescriptor> desc;
  while (!(desc = manager->TakeReplayTexture(aTextureId))) {
    if (NS_WARN_IF(!manager->CanSend())) {
      return nullptr;
    }

    if (NS_WARN_IF(!sReplayTexturesEnabled)) {
      return nullptr;
    }

    TimeDuration timeout = TimeDuration::FromMilliseconds(
        StaticPrefs::gfx_canvas_remote_texture_timeout_ms());
    CVStatus status = lock.Wait(timeout);
    if (status == CVStatus::Timeout) {
      // If something has gone wrong and the texture has already been destroyed,
      // it will have cleaned up its descriptor.
      return nullptr;
    }
  }

  return desc;
}

CanvasManagerParent::CanvasManagerParent(const dom::ContentParentId& aContentId)
    : mContentId(aContentId) {}

CanvasManagerParent::~CanvasManagerParent() = default;

void CanvasManagerParent::Bind(Endpoint<PCanvasManagerParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    NS_WARNING("Failed to bind CanvasManagerParent!");
    return;
  }

  StaticMonitorAutoLock lock(sManagerMonitor);
  MOZ_RELEASE_ASSERT(
      sManagers.try_emplace(uint64_t(mContentId), RefPtr{this}).second);
}

void CanvasManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  StaticMonitorAutoLock lock(sManagerMonitor);
  sManagers.erase(uint64_t(mContentId));
  sManagerMonitor.NotifyAll();
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
  return MakeAndAddRef<layers::CanvasTranslator>(mContentId);
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvGetSnapshot(
    const uint32_t& aManagerId, const int32_t& aProtocolId,
    const Maybe<RemoteTextureOwnerId>& aOwnerId,
    webgl::FrontBufferSnapshotIpc* aResult) {
  if (!aManagerId) {
    return IPC_FAIL(this, "invalid id");
  }

  IProtocol* actor = nullptr;

  {
    StaticMonitorAutoLock lock(sManagerMonitor);
    for (const auto& i : sManagers) {
      const auto& manager = i.second;
      if (manager->mContentId == mContentId && manager->mId == aManagerId) {
        actor = manager->Lookup(aProtocolId);
        break;
      }
    }
  }

  if (!actor) {
    return IPC_FAIL(this, "invalid actor");
  }

  if (actor->GetSide() != mozilla::ipc::Side::ParentSide) {
    return IPC_FAIL(this, "unsupported actor");
  }

  webgl::FrontBufferSnapshotIpc buffer;
  switch (actor->GetProtocolId()) {
    case ProtocolId::PWebGLMsgStart: {
      RefPtr<dom::WebGLParent> webgl = static_cast<dom::WebGLParent*>(actor);
      mozilla::ipc::IPCResult rv = webgl->GetFrontBufferSnapshot(&buffer, this);
      if (!rv) {
        return rv;
      }
    } break;
    case ProtocolId::PWebGPUMsgStart: {
      RefPtr<webgpu::WebGPUParent> webgpu =
          static_cast<webgpu::WebGPUParent*>(actor);
      IntSize size;
      if (aOwnerId.isNothing()) {
        return IPC_FAIL(this, "invalid OwnerId");
      }
      mozilla::ipc::IPCResult rv =
          webgpu->GetFrontBufferSnapshot(this, *aOwnerId, buffer.shmem, size);
      if (!rv) {
        return rv;
      }
      buffer.surfSize.x = static_cast<uint32_t>(size.width);
      buffer.surfSize.y = static_cast<uint32_t>(size.height);
    } break;
    default:
      return IPC_FAIL(this, "unsupported protocol");
  }

  *aResult = std::move(buffer);
  return IPC_OK();
}

}  // namespace mozilla::gfx
