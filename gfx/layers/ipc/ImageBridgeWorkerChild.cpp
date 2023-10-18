/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageBridgeWorkerChild.h"

#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/PImageBridgeParent.h"

namespace mozilla::layers {

MOZ_THREAD_LOCAL(ImageBridgeWorkerChild*) ImageBridgeWorkerChild::sLocalBridge;
Atomic<uint32_t> ImageBridgeWorkerChild::sNextCompositableNamespace(0);

ImageBridgeWorkerChild::ImageBridgeWorkerChild(uint32_t aNamespace,
                                               uint32_t aCompositableNamespace)
    : ImageBridgeChild(aNamespace, aCompositableNamespace) {}

ImageBridgeWorkerChild::~ImageBridgeWorkerChild() = default;

void ImageBridgeWorkerChild::Destroy() {
  // The caller has a strong reference. ActorDestroy will clear sLocalManager.
  Close();
  mWorkerRef = nullptr;
}

void ImageBridgeWorkerChild::ActorDestroy(ActorDestroyReason aReason) {
  ImageBridgeChild::ActorDestroy(aReason);
  if (sLocalBridge.get() == this) {
    sLocalBridge.set(nullptr);
  }
}

/* static */ ImageBridgeWorkerChild* ImageBridgeWorkerChild::Get(
    dom::WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT(aWorkerPrivate);
  MOZ_ASSERT(aWorkerPrivate->IsOnCurrentThread());

  if (NS_WARN_IF(!sLocalBridge.init())) {
    return nullptr;
  }

  ImageBridgeWorkerChild* bridgeWeak = sLocalBridge.get();
  if (bridgeWeak) {
    return bridgeWeak;
  }

  RefPtr<ImageBridgeChild> bridgeSingleton =
      ImageBridgeChild::GetTrueSingleton();
  if (NS_WARN_IF(!bridgeSingleton)) {
    return nullptr;
  }

  ipc::Endpoint<PImageBridgeParent> parentEndpoint;
  ipc::Endpoint<PImageBridgeChild> childEndpoint;

  auto compositorPid = CompositorManagerChild::GetOtherPid();
  if (NS_WARN_IF(!compositorPid)) {
    return nullptr;
  }

  nsresult rv = PImageBridge::CreateEndpoints(
      compositorPid, base::GetCurrentProcId(), &parentEndpoint, &childEndpoint);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  uint32_t cns = ++sNextCompositableNamespace;
  MOZ_RELEASE_ASSERT(cns != UINT32_MAX);
  RefPtr<ImageBridgeWorkerChild> bridge =
      new ImageBridgeWorkerChild(bridgeSingleton->GetNamespace(), cns);

  // The IPCWorkerRef will let us know when the worker is shutting down. This
  // will let us clear our threadlocal reference and close the actor. We rely
  // upon an explicit shutdown for the main thread.
  bridge->mWorkerRef =
      dom::IPCWorkerRef::Create(aWorkerPrivate, "ImageBridgeWorkerChild",
                                [bridge]() { bridge->Destroy(); });
  if (NS_WARN_IF(!bridge->mWorkerRef)) {
    return nullptr;
  }

  if (NS_WARN_IF(!childEndpoint.Bind(bridge))) {
    return nullptr;
  }

  // We can't talk to the compositor process directly from worker threads, but
  // the main thread can via CompositorManagerChild.
  aWorkerPrivate->DispatchToMainThread(NS_NewRunnableFunction(
      "ImageBridgeWorkerChild::CreateParent",
      [parentEndpoint = std::move(parentEndpoint), cns]() mutable {
        CreateParent(std::move(parentEndpoint), cns);
      }));

  bridge->mCanSend = true;
  sLocalBridge.set(bridge);
  return bridge;
}

/* static */ bool ImageBridgeWorkerChild::CreateParent(
    ipc::Endpoint<PImageBridgeParent>&& aEndpoint,
    uint32_t aCompositableNamespace) {
  MOZ_ASSERT(NS_IsMainThread());

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (!manager || !manager->CanSend()) {
    return false;
  }

  return manager->SendInitImageBridge(std::move(aEndpoint),
                                      aCompositableNamespace);
}

nsCOMPtr<nsISerialEventTarget> ImageBridgeWorkerChild::GetThread() const {
  if (!mWorkerRef) {
    return GetCurrentSerialEventTarget();
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return GetCurrentSerialEventTarget();
}

RefPtr<ImageClient> ImageBridgeWorkerChild::CreateImageClient(
    CompositableType aType, ImageContainer* aImageContainer) {
  if (!mWorkerRef) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return ImageBridgeChild::CreateImageClientNow(aType, aImageContainer);
}

void ImageBridgeWorkerChild::UpdateImageClient(
    RefPtr<ImageContainer> aContainer) {
  if (!mWorkerRef) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  ImageBridgeChild::UpdateImageClient(aContainer);
}

void ImageBridgeWorkerChild::UpdateCompositable(
    const RefPtr<ImageContainer> aContainer, const RemoteTextureId aTextureId,
    const RemoteTextureOwnerId aOwnerId, const gfx::IntSize aSize,
    const TextureFlags aFlags) {
  if (!mWorkerRef) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  ImageBridgeChild::UpdateCompositable(aContainer, aTextureId, aOwnerId, aSize,
                                       aFlags);
}

void ImageBridgeWorkerChild::FlushAllImages(ImageClient* aClient,
                                            ImageContainer* aContainer) {
  if (!mWorkerRef) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  ImageBridgeChild::FlushAllImagesNow(aClient, aContainer);
}

already_AddRefed<TextureReadLock>
ImageBridgeWorkerChild::CreateBlockingTextureReadLock() {
  if (!mWorkerRef) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return TextureForwarder::CreateBlockingTextureReadLock();
}

already_AddRefed<TextureReadLock>
ImageBridgeWorkerChild::CreateNonBlockingTextureReadLock() {
  if (!mWorkerRef) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return TextureForwarder::CreateNonBlockingTextureReadLock();
}

void ImageBridgeWorkerChild::ReleaseCompositable(
    const CompositableHandle& aHandle) {
  if (!mWorkerRef) {
    return;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  ImageBridgeChild::ReleaseCompositable(aHandle);
}

bool ImageBridgeWorkerChild::AllocUnsafeShmem(size_t aSize,
                                              mozilla::ipc::Shmem* aShmem) {
  if (!mWorkerRef || !CanSend()) {
    return false;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return PImageBridgeChild::AllocUnsafeShmem(aSize, aShmem);
}

bool ImageBridgeWorkerChild::AllocShmem(size_t aSize,
                                        mozilla::ipc::Shmem* aShmem) {
  if (!mWorkerRef || !CanSend()) {
    return false;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return PImageBridgeChild::AllocShmem(aSize, aShmem);
}

bool ImageBridgeWorkerChild::DeallocShmem(mozilla::ipc::Shmem& aShmem) {
  if (!mWorkerRef || !CanSend()) {
    return false;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return PImageBridgeChild::DeallocShmem(aShmem);
}

bool ImageBridgeWorkerChild::InForwarderThread() {
  if (!mWorkerRef) {
    return false;
  }

  MOZ_RELEASE_ASSERT(mWorkerRef->Private()->IsOnCurrentThread());
  return true;
}

}  // namespace mozilla::layers
