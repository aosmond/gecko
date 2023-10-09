/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasManagerParent.h"
#include "gfxPlatform.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/WebGLParent.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CanvasTranslator.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

using namespace mozilla::layers;

namespace mozilla::gfx {

class MOZ_STACK_CLASS AutoCanvasManagerParentAsyncMessageSender final {
 public:
  explicit AutoCanvasManagerParentAsyncMessageSender(
      CanvasManagerParent* aCanvasManager,
      nsTArray<OpDestroy>* aToDestroy = nullptr)
      : mCanvasManager(aCanvasManager), mToDestroy(aToDestroy) {
    mCanvasManager->SetAboutToSendAsyncMessages();
  }

  ~AutoCanvasManagerParentAsyncMessageSender() {
    mCanvasManager->SendPendingAsyncMessages();
    if (mToDestroy) {
      for (const auto& op : *mToDestroy) {
        mCanvasManager->DestroyActor(op);
      }
    }
  }

 private:
  CanvasManagerParent* mCanvasManager;
  nsTArray<OpDestroy>* mToDestroy;
};

CanvasManagerParent::ManagerSet CanvasManagerParent::sManagers;

StaticMonitor CanvasManagerParent::sReplayTexturesMonitor;
nsTArray<CanvasManagerParent::ReplayTexture>
    CanvasManagerParent::sReplayTextures;
bool CanvasManagerParent::sReplayTexturesEnabled(true);

/* static */ void CanvasManagerParent::Init(
    Endpoint<PCanvasManagerParent>&& aEndpoint) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  auto manager = MakeRefPtr<CanvasManagerParent>();

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

  StaticMonitorAutoLock lock(sReplayTexturesMonitor);
  sReplayTextures.Clear();
  lock.NotifyAll();
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

  AutoTArray<RefPtr<CanvasTranslator>, 16> actors;
  for (const auto& manager : sManagers) {
    for (const auto& canvas : manager->ManagedPCanvasParent()) {
      actors.AppendElement(static_cast<CanvasTranslator*>(canvas));
    }
  }

  {
    StaticMonitorAutoLock lock(sReplayTexturesMonitor);
    sReplayTexturesEnabled = false;
    sReplayTextures.Clear();
  }

  for (const auto& actor : actors) {
    Unused << NS_WARN_IF(!actor->SendDeactivate());
  }

  {
    StaticMonitorAutoLock lock(sReplayTexturesMonitor);
    lock.NotifyAll();
  }
}

/* static */ void CanvasManagerParent::AddReplayTexture(
    CanvasTranslator* aOwner, int64_t aTextureId, TextureData* aTextureData) {
  auto desc = MakeUnique<SurfaceDescriptor>();
  if (!aTextureData->Serialize(*desc)) {
    MOZ_CRASH("Failed to serialize");
  }

  StaticMonitorAutoLock lock(sReplayTexturesMonitor);
  sReplayTextures.AppendElement(
      ReplayTexture{aOwner, aTextureId, std::move(desc)});
  lock.NotifyAll();
}

/* static */ void CanvasManagerParent::RemoveReplayTexture(
    CanvasTranslator* aOwner, int64_t aTextureId) {
  StaticMonitorAutoLock lock(sReplayTexturesMonitor);

  auto i = sReplayTextures.Length();
  while (i > 0) {
    --i;
    const auto& texture = sReplayTextures[i];
    if (texture.mOwner == aOwner && texture.mId == aTextureId) {
      sReplayTextures.RemoveElementAt(i);
      break;
    }
  }
}

/* static */ void CanvasManagerParent::RemoveReplayTextures(
    CanvasTranslator* aOwner) {
  StaticMonitorAutoLock lock(sReplayTexturesMonitor);

  auto i = sReplayTextures.Length();
  while (i > 0) {
    --i;
    const auto& texture = sReplayTextures[i];
    if (texture.mOwner == aOwner) {
      sReplayTextures.RemoveElementAt(i);
    }
  }
}

/* static */ UniquePtr<SurfaceDescriptor>
CanvasManagerParent::TakeReplayTexture(base::ProcessId aOtherPid,
                                       int64_t aTextureId) {
  // While in theory this could be relatively expensive, the array is most
  // likely very small as the textures are removed during each composite.
  auto i = sReplayTextures.Length();
  while (i > 0) {
    --i;
    const auto& texture = sReplayTextures[i];
    if (texture.mOwner->OtherPid() == aOtherPid && texture.mId == aTextureId) {
      UniquePtr<SurfaceDescriptor> desc = std::move(sReplayTextures[i].mDesc);
      sReplayTextures.RemoveElementAt(i);
      return desc;
    }
  }
  return nullptr;
}

/* static */ UniquePtr<SurfaceDescriptor>
CanvasManagerParent::WaitForReplayTexture(base::ProcessId aOtherPid,
                                          int64_t aTextureId) {
  StaticMonitorAutoLock lock(sReplayTexturesMonitor);

  UniquePtr<SurfaceDescriptor> desc;
  while (!(desc = TakeReplayTexture(aOtherPid, aTextureId))) {
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

CanvasManagerParent::CanvasManagerParent() = default;
CanvasManagerParent::~CanvasManagerParent() = default;

void CanvasManagerParent::Bind(Endpoint<PCanvasManagerParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    NS_WARNING("Failed to bind CanvasManagerParent!");
    return;
  }

  sManagers.Insert(this);
}

void CanvasManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  sManagers.Remove(this);
}

already_AddRefed<dom::PWebGLParent> CanvasManagerParent::AllocPWebGLParent() {
  return MakeAndAddRef<dom::WebGLParent>();
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

already_AddRefed<PCanvasParent> CanvasManagerParent::AllocPCanvasParent() {
  return MakeAndAddRef<CanvasTranslator>();
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvGetSnapshot(
    const uint32_t& aManagerId, const int32_t& aProtocolId,
    const Maybe<RemoteTextureOwnerId>& aOwnerId,
    webgl::FrontBufferSnapshotIpc* aResult) {
  if (!aManagerId) {
    return IPC_FAIL(this, "invalid id");
  }

  IProtocol* actor = nullptr;
  for (CanvasManagerParent* i : sManagers) {
    if (i->OtherPidMaybeInvalid() == OtherPidMaybeInvalid() &&
        i->mId == aManagerId) {
      actor = i->Lookup(aProtocolId);
      break;
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

mozilla::ipc::IPCResult CanvasManagerParent::RecvNewCompositable(
    const CompositableHandle& aHandle, const TextureInfo& aInfo) {
  RefPtr<CompositableHost> host = AddCompositable(aHandle, aInfo);
  if (!host) {
    return IPC_FAIL_NO_REASON(this);
  }

  host->SetAsyncRef(AsyncCompositableRef(OtherPid(), aHandle));
  return IPC_OK();
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvReleaseCompositable(
    const CompositableHandle& aHandle) {
  ReleaseCompositable(aHandle);
  return IPC_OK();
}

mozilla::ipc::IPCResult CanvasManagerParent::RecvUpdate(
    EditArray&& aEdits, OpDestroyArray&& aToDestroy,
    const uint64_t& aFwdTransactionId) {
  AUTO_PROFILER_TRACING_MARKER("Paint", "CanvasManagerTransaction", GRAPHICS);
  AUTO_PROFILER_LABEL("CanvasManagerParent::RecvUpdate", GRAPHICS);

  // This ensures that destroy operations are always processed. It is not safe
  // to early-return from RecvUpdate without doing so.
  AutoCanvasManagerParentAsyncMessageSender autoAsyncMessageSender(this,
                                                                   &aToDestroy);
  UpdateFwdTransactionId(aFwdTransactionId);

  for (const auto& edit : aEdits) {
    RefPtr<CompositableHost> compositable =
        FindCompositable(edit.compositable());
    if (!compositable ||
        !ReceiveCompositableUpdate(edit.detail(), WrapNotNull(compositable),
                                   edit.compositable())) {
      return IPC_FAIL_NO_REASON(this);
    }
    uint32_t dropped = compositable->GetDroppedFrames();
    if (dropped) {
      Unused << SendReportFramesDropped(edit.compositable(), dropped);
    }
  }

  return IPC_OK();
}

PTextureParent* CanvasManagerParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const uint64_t& aSerial, const wr::MaybeExternalImageId& aExternalImageId) {
  return TextureHost::CreateIPDLActor(this, aSharedData, std::move(aReadLock),
                                      aLayersBackend, aFlags, aSerial,
                                      aExternalImageId);
}

bool CanvasManagerParent::DeallocPTextureParent(PTextureParent* actor) {
  return TextureHost::DestroyIPDLActor(actor);
}

void CanvasManagerParent::NotifyNotUsed(PTextureParent* aTexture,
                                        uint64_t aTransactionId) {
  RefPtr<TextureHost> texture = TextureHost::AsTextureHost(aTexture);
  if (!texture) {
    return;
  }

  if (!(texture->GetFlags() & TextureFlags::RECYCLE) &&
      !(texture->GetFlags() & TextureFlags::WAIT_HOST_USAGE_END)) {
    return;
  }

  uint64_t textureId = TextureHost::GetTextureSerial(aTexture);
  mPendingAsyncMessage.push_back(OpNotifyNotUsed(textureId, aTransactionId));

  if (!IsAboutToSendAsyncMessages()) {
    SendPendingAsyncMessages();
  }
}

void CanvasManagerParent::SendAsyncMessage(
    const nsTArray<AsyncParentMessageData>& aMessage) {
  Unused << SendParentAsyncMessages(aMessage);
}

bool CanvasManagerParent::AllocShmem(size_t aSize, ipc::Shmem* aShmem) {
  if (!CanSend()) {
    return false;
  }
  return PCanvasManagerParent::AllocShmem(aSize, aShmem);
}

bool CanvasManagerParent::AllocUnsafeShmem(size_t aSize, ipc::Shmem* aShmem) {
  if (!CanSend()) {
    return false;
  }
  return PCanvasManagerParent::AllocUnsafeShmem(aSize, aShmem);
}

bool CanvasManagerParent::DeallocShmem(ipc::Shmem& aShmem) {
  if (!CanSend()) {
    return false;
  }
  return PCanvasManagerParent::DeallocShmem(aShmem);
}

}  // namespace mozilla::gfx
