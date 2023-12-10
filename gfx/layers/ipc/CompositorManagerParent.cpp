/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/CanvasManagerParent.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/ContentCompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "gfxPlatform.h"
#include "VsyncSource.h"

namespace mozilla {
namespace layers {

StaticMonitor CompositorManagerParent::sMonitor;
StaticRefPtr<CompositorManagerParent> CompositorManagerParent::sInstance;
CompositorManagerParent::ManagerMap CompositorManagerParent::sManagers;

/* static */
already_AddRefed<CompositorManagerParent>
CompositorManagerParent::CreateSameProcess(uint32_t aNamespace) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMonitorAutoLock lock(sMonitor);

  // We are creating a manager for the UI process, inside the combined GPU/UI
  // process. It is created more-or-less the same but we retain a reference to
  // the parent to access state.
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initialized");
    return nullptr;
  }

  // The child is responsible for setting up the IPC channel in the same
  // process case because if we open from the child perspective, we can do it
  // on the main thread and complete before we return the manager handles.
  RefPtr<CompositorManagerParent> parent =
      new CompositorManagerParent(dom::ContentParentId(), aNamespace);
  parent->SetOtherProcessId(base::GetCurrentProcId());
  return parent.forget();
}

/* static */
bool CompositorManagerParent::Create(
    Endpoint<PCompositorManagerParent>&& aEndpoint,
    dom::ContentParentId aChildId, uint32_t aNamespace, bool aIsRoot) {
  MOZ_ASSERT(NS_IsMainThread());

  // We are creating a manager for the another process, inside the GPU process
  // (or UI process if it subsumbed the GPU process).
  MOZ_ASSERT(aEndpoint.OtherPid() != base::GetCurrentProcId());

  if (!CompositorThreadHolder::IsActive()) {
    return false;
  }

  RefPtr<CompositorManagerParent> bridge =
      new CompositorManagerParent(aChildId, aNamespace);

  RefPtr<Runnable> runnable =
      NewRunnableMethod<Endpoint<PCompositorManagerParent>&&, bool>(
          "CompositorManagerParent::Bind", bridge,
          &CompositorManagerParent::Bind, std::move(aEndpoint), aIsRoot);
  CompositorThread()->Dispatch(runnable.forget());
  return true;
}

/* static */
already_AddRefed<CompositorBridgeParent>
CompositorManagerParent::CreateSameProcessWidgetCompositorBridge(
    CSSToLayoutDeviceScale aScale, const CompositorOptions& aOptions,
    bool aUseExternalSurfaceSize, const gfx::IntSize& aSurfaceSize,
    uint64_t aInnerWindowId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  // When we are in a combined UI / GPU process, InProcessCompositorSession
  // requires both the parent and child PCompositorBridge actors for its own
  // construction, which is done on the main thread. Normally
  // CompositorBridgeParent is created on the compositor thread via the IPDL
  // plumbing (CompositorManagerParent::AllocPCompositorBridgeParent). Thus to
  // actually get a reference to the parent, we would need to block on the
  // compositor thread until it handles our constructor message. Because only
  // one one IPDL constructor is permitted per parent and child protocol, we
  // cannot make the normal case async and this case sync. Instead what we do
  // is leave the constructor async (a boon to the content process setup) and
  // create the parent ahead of time. It will pull the preinitialized parent
  // from the queue when it receives the message and give that to IPDL.

  // Note that the static mutex not only is used to protect sInstance, but also
  // mPendingCompositorBridges.
  StaticMonitorAutoLock lock(sMonitor);
  if (NS_WARN_IF(!sInstance)) {
    return nullptr;
  }

  TimeDuration vsyncRate =
      gfxPlatform::GetPlatform()->GetGlobalVsyncDispatcher()->GetVsyncRate();

  RefPtr<CompositorBridgeParent> bridge = new CompositorBridgeParent(
      sInstance, aScale, vsyncRate, aOptions, aUseExternalSurfaceSize,
      aSurfaceSize, aInnerWindowId);

  sInstance->mPendingCompositorBridges.AppendElement(bridge);
  return bridge.forget();
}

CompositorManagerParent::CompositorManagerParent(
    dom::ContentParentId aContentId, uint32_t aNamespace)
    : mCompositorThreadHolder(CompositorThreadHolder::GetSingleton()),
      mContentId(aContentId),
      mNamespace(aNamespace) {}

CompositorManagerParent::~CompositorManagerParent() = default;

void CompositorManagerParent::Bind(
    Endpoint<PCompositorManagerParent>&& aEndpoint, bool aIsRoot) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return;
  }

  BindComplete(aIsRoot);
}

void CompositorManagerParent::BindComplete(bool aIsRoot) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread() ||
             NS_IsMainThread());

  StaticMonitorAutoLock lock(sMonitor);
  if (aIsRoot) {
    MOZ_ASSERT(!sInstance);
    sInstance = this;
  }

  MOZ_RELEASE_ASSERT(sManagers.try_emplace(mNamespace, this).second);
}

void CompositorManagerParent::ActorDestroy(ActorDestroyReason aReason) {
  SharedSurfacesParent::DestroyProcess(OtherPid());

  GetCurrentSerialEventTarget()->Dispatch(
      NewRunnableMethod("layers::CompositorManagerParent::DeferredDestroy",
                        this, &CompositorManagerParent::DeferredDestroy));

  StaticMonitorAutoLock lock(sMonitor);
  if (sInstance == this) {
    sInstance = nullptr;
  }

  MOZ_RELEASE_ASSERT(sManagers.erase(mNamespace) > 0);
}

void CompositorManagerParent::DeferredDestroy() {
  mCompositorThreadHolder = nullptr;
}

/* static */
void CompositorManagerParent::ShutdownInternal() {
  nsTArray<RefPtr<CompositorManagerParent>> actors;

  // We move here because we may attempt to acquire the same lock during the
  // destroy to remove the reference in sManagers.
  {
    StaticMonitorAutoLock lock(sMonitor);
    actors.SetCapacity(sManagers.size());
    for (auto& i : sManagers) {
      actors.AppendElement(i.second);
    }
  }

  for (auto& actor : actors) {
    actor->Close();
  }
}

/* static */
void CompositorManagerParent::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  CompositorThread()->Dispatch(NS_NewRunnableFunction(
      "layers::CompositorManagerParent::Shutdown",
      []() -> void { CompositorManagerParent::ShutdownInternal(); }));
}

/* static */ void CompositorManagerParent::WaitForPing(
    const wr::ExternalImageId& aId) {
  MOZ_ASSERT(!CompositorThreadHolder::IsInCompositorThread());

  // When a SourceSurfaceSharedData is created, it will always dispatch to the
  // main thread to be shared via the CompositorManagerChild with the compositor
  // process. Provided we send a ping from a non-Compositor thread in the
  // compositor process, the ping can only be processed after clearing the main
  // thread's event queue, and any incoming IPDL messages. So if the caller came
  // too early looking for an external image, we can just lookup the owning
  // CompositorManagerParent, issue and ping and block until the ping returns.

  bool complete = false;

  auto resolve = [&](void_t&&) {
    StaticMonitorAutoLock lock(sMonitor);
    complete = true;
    lock.NotifyAll();
  };

  auto reject = [&](ipc::ResponseRejectReason) {
    StaticMonitorAutoLock lock(sMonitor);
    complete = true;
    lock.NotifyAll();
  };

  CompositorThread()->Dispatch(
      NS_NewRunnableFunction("CompositorManagerParent::Ping", [&]() {
        StaticMonitorAutoLock lock(sMonitor);

        uint32_t extNamespace = static_cast<uint32_t>(wr::AsUint64(aId) >> 32);
        auto i = sManagers.find(extNamespace);
        if (i == sManagers.end()) {
          reject(ipc::ResponseRejectReason::ChannelClosed);
          return;
        }

        i->second->SendPing(std::move(resolve), std::move(reject));
      }));

  StaticMonitorAutoLock lock(sMonitor);
  while (!complete) {
    lock.Wait();
  }
}

already_AddRefed<PCompositorBridgeParent>
CompositorManagerParent::AllocPCompositorBridgeParent(
    const CompositorBridgeOptions& aOpt) {
  switch (aOpt.type()) {
    case CompositorBridgeOptions::TContentCompositorOptions: {
      RefPtr<ContentCompositorBridgeParent> bridge =
          new ContentCompositorBridgeParent(this);
      return bridge.forget();
    }
    case CompositorBridgeOptions::TWidgetCompositorOptions: {
      // Only the UI process is allowed to create widget compositors in the
      // compositor process.
      gfx::GPUParent* gpu = gfx::GPUParent::GetSingleton();
      if (NS_WARN_IF(!gpu || OtherPid() != gpu->OtherPid())) {
        MOZ_ASSERT_UNREACHABLE("Child cannot create widget compositor!");
        break;
      }

      const WidgetCompositorOptions& opt = aOpt.get_WidgetCompositorOptions();
      RefPtr<CompositorBridgeParent> bridge = new CompositorBridgeParent(
          this, opt.scale(), opt.vsyncRate(), opt.options(),
          opt.useExternalSurfaceSize(), opt.surfaceSize(), opt.innerWindowId());
      return bridge.forget();
    }
    case CompositorBridgeOptions::TSameProcessWidgetCompositorOptions: {
      // If the GPU and UI process are combined, we actually already created the
      // CompositorBridgeParent, so we need to reuse that to inject it into the
      // IPDL framework.
      if (NS_WARN_IF(OtherPid() != base::GetCurrentProcId())) {
        MOZ_ASSERT_UNREACHABLE("Child cannot create same process compositor!");
        break;
      }

      // Note that the static mutex not only is used to protect sInstance, but
      // also mPendingCompositorBridges.
      StaticMonitorAutoLock lock(sMonitor);
      if (mPendingCompositorBridges.IsEmpty()) {
        break;
      }

      RefPtr<CompositorBridgeParent> bridge = mPendingCompositorBridges[0];
      mPendingCompositorBridges.RemoveElementAt(0);
      return bridge.forget();
    }
    default:
      break;
  }

  return nullptr;
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvAddSharedSurface(
    const wr::ExternalImageId& aId, SurfaceDescriptorShared&& aDesc) {
  if (NS_WARN_IF(!OwnsExternalImageId(aId))) {
    return IPC_OK();
  }

  SharedSurfacesParent::Add(aId, std::move(aDesc), OtherPid());
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvRemoveSharedSurface(
    const wr::ExternalImageId& aId) {
  if (NS_WARN_IF(!OwnsExternalImageId(aId))) {
    return IPC_OK();
  }

  SharedSurfacesParent::Remove(aId);
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvReportSharedSurfacesMemory(
    ReportSharedSurfacesMemoryResolver&& aResolver) {
  SharedSurfacesMemoryReport report;
  SharedSurfacesParent::AccumulateMemoryReport(OtherPid(), report);
  aResolver(std::move(report));
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvNotifyMemoryPressure() {
  nsTArray<PCompositorBridgeParent*> compositorBridges;
  ManagedPCompositorBridgeParent(compositorBridges);
  for (auto bridge : compositorBridges) {
    static_cast<CompositorBridgeParentBase*>(bridge)->NotifyMemoryPressure();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvReportMemory(
    ReportMemoryResolver&& aResolver) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MemoryReport aggregate;
  PodZero(&aggregate);

  // Accumulate RenderBackend usage.
  nsTArray<PCompositorBridgeParent*> compositorBridges;
  ManagedPCompositorBridgeParent(compositorBridges);
  for (auto bridge : compositorBridges) {
    static_cast<CompositorBridgeParentBase*>(bridge)->AccumulateMemoryReport(
        &aggregate);
  }

  // Accumulate Renderer usage asynchronously, and resolve.
  //
  // Note that the IPDL machinery requires aResolver to be called on this
  // thread, so we can't just pass it over to the renderer thread. We use
  // an intermediate MozPromise instead.
  wr::RenderThread::AccumulateMemoryReport(aggregate)->Then(
      CompositorThread(), __func__,
      [resolver = std::move(aResolver)](MemoryReport aReport) {
        resolver(aReport);
      },
      [](bool) {
        MOZ_ASSERT_UNREACHABLE("MemoryReport promises are never rejected");
      });

  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorManagerParent::RecvInitCanvasManager(
    Endpoint<PCanvasManagerParent>&& aEndpoint) {
  gfx::CanvasManagerParent::Init(std::move(aEndpoint), mContentId);
  return IPC_OK();
}

/* static */
void CompositorManagerParent::NotifyWebRenderError(wr::WebRenderError aError) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  StaticMonitorAutoLock lock(sMonitor);
  if (NS_WARN_IF(!sInstance)) {
    return;
  }
  Unused << sInstance->SendNotifyWebRenderError(aError);
}

}  // namespace layers
}  // namespace mozilla
