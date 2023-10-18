/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEBRIDGEWORKERCHILD_H
#define MOZILLA_GFX_IMAGEBRIDGEWORKERCHILD_H

#include "mozilla/Atomics.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/ThreadLocal.h"

namespace mozilla {
namespace dom {
class IPCWorkerRef;
class WorkerPrivate;
}  // namespace dom

namespace layers {

/**
 * ImageBridgeWorkerChild is a specialized ImageBridgeChild for DOM workers.
 */
class ImageBridgeWorkerChild final : public ImageBridgeChild {
 public:
  static ImageBridgeWorkerChild* Get(dom::WorkerPrivate* aWorkerPrivate);

  void ActorDestroy(ActorDestroyReason aReason) override;

  nsCOMPtr<nsISerialEventTarget> GetThread() const override;

  RefPtr<ImageClient> CreateImageClient(
      CompositableType aType, ImageContainer* aImageContainer) override;

  void UpdateImageClient(RefPtr<ImageContainer> aContainer) override;

  void UpdateCompositable(const RefPtr<ImageContainer> aContainer,
                          const RemoteTextureId aTextureId,
                          const RemoteTextureOwnerId aOwnerId,
                          const gfx::IntSize aSize,
                          const TextureFlags aFlags) override;

  void FlushAllImages(ImageClient* aClient,
                      ImageContainer* aContainer) override;

  already_AddRefed<TextureReadLock> CreateBlockingTextureReadLock() override;
  already_AddRefed<TextureReadLock> CreateNonBlockingTextureReadLock() override;

 protected:
  ~ImageBridgeWorkerChild() override;
  void Destroy();

 public:
  void ReleaseCompositable(const CompositableHandle& aHandle) override;

  // ISurfaceAllocator
  bool AllocUnsafeShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool AllocShmem(size_t aSize, mozilla::ipc::Shmem* aShmem) override;
  bool DeallocShmem(mozilla::ipc::Shmem& aShmem) override;

  bool InForwarderThread() override;

 protected:
  static bool CreateParent(ipc::Endpoint<PImageBridgeParent>&& aEndpoint,
                           uint32_t aCompositableNamespace);

  explicit ImageBridgeWorkerChild(uint32_t aNamespace,
                                  uint32_t aCompositableNamespace);

  RefPtr<dom::IPCWorkerRef> mWorkerRef;

  static MOZ_THREAD_LOCAL(ImageBridgeWorkerChild*) sLocalBridge;
  static Atomic<uint32_t> sNextCompositableNamespace;
};

}  // namespace layers
}  // namespace mozilla

#endif
