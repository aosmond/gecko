/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_CanvasManagerChild_h__
#define _include_gfx_ipc_CanvasManagerChild_h__

#include "mozilla/Atomics.h"
#include "mozilla/gfx/PCanvasManagerChild.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/CompositableForwarder.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/ThreadLocal.h"
#include <unordered_map>

namespace mozilla {
namespace dom {
class IPCWorkerRef;
class WorkerPrivate;
}  // namespace dom

namespace layers {
class CanvasChild;
}  // namespace layers

namespace webgpu {
class WebGPUChild;
}  // namespace webgpu

namespace gfx {
class DataSourceSurface;

class CanvasManagerChild final : public PCanvasManagerChild,
                                 public layers::CompositableForwarder,
                                 public layers::TextureForwarder {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CanvasManagerChild, override);
  NS_DECL_OWNINGTHREAD;

  typedef nsTArray<AsyncParentMessageData> AsyncParentMessageArray;

  explicit CanvasManagerChild(uint32_t aId);
  uint32_t Id() const { return mId; }
  already_AddRefed<DataSourceSurface> GetSnapshot(
      uint32_t aManagerId, int32_t aProtocolId,
      const Maybe<RemoteTextureOwnerId>& aOwnerId, SurfaceFormat aFormat,
      bool aPremultiply, bool aYFlip);
  void ActorDestroy(ActorDestroyReason aReason) override;

  static CanvasManagerChild* Get();
  static void Shutdown();
  static bool CreateParent(
      mozilla::ipc::Endpoint<PCanvasManagerParent>&& aEndpoint);

  bool IsCanvasActive() { return mActive; }
  void EndCanvasTransaction();
  void DeactivateCanvas();

  RefPtr<layers::CanvasChild> GetCanvasChild();

  RefPtr<webgpu::WebGPUChild> GetWebGPUChild();

  PTextureChild* AllocPTextureChild(
      const layers::SurfaceDescriptor&, layers::ReadLockDescriptor&,
      const layers::LayersBackend&, const layers::TextureFlags&,
      const uint64_t& aSerial,
      const wr::MaybeExternalImageId& aExternalImageId);
  bool DeallocPTextureChild(PTextureChild* actor);

  mozilla::ipc::IPCResult RecvParentAsyncMessages(
      nsTArray<AsyncParentMessageData>&& aMessages);

  // CompositableForwarder
  void Connect(layers::CompositableClient* aCompositable,
               layers::ImageContainer* aImageContainer) override;
  bool UsesTextureChildLock() const override { return true; }
  void UseTextures(layers::CompositableClient* aCompositable,
                   const nsTArray<TimedTextureClient>& aTextures) override;
  void UseRemoteTexture(layers::CompositableClient* aCompositable,
                        const RemoteTextureId aTextureId,
                        const RemoteTextureOwnerId aOwnerId,
                        const gfx::IntSize aSize,
                        const layers::TextureFlags aFlags) override;
  void EnableRemoteTexturePushCallback(
      layers::CompositableClient* aCompositable,
      const layers::RemoteTextureOwnerId aOwnerId, const gfx::IntSize aSize,
      const TextureFlags aFlags) override;
  void ReleaseCompositable(const layers::CompositableHandle& aHandle) override;
  void CancelWaitForNotifyNotUsed(uint64_t aTextureId) override;
  bool DestroyInTransaction(PTextureChild* aTexture) override;
  void RemoveTextureFromCompositable(layers::CompositableClient* aCompositable,
                                     layers::TextureClient* aTexture) override;

  /**
   * Hold TextureClient ref until end of usage on host side if
   * TextureFlags::RECYCLE is set. Host side's usage is checked via
   * CompositableRef.
   */
  void HoldUntilCompositableRefReleasedIfNecessary(
      layers::TextureClient* aClient);

  /**
   * Notify id of Texture When host side end its use. Transaction id is used to
   * make sure if there is no newer usage.
   */
  void NotifyNotUsed(uint64_t aTextureId, uint64_t aFwdTransactionId);

  bool DestroyInTransaction(const layers::CompositableHandle& aHandle);

 private:
  ~CanvasManagerChild();
  void Destroy();

  RefPtr<mozilla::dom::IPCWorkerRef> mWorkerRef;
  RefPtr<layers::CanvasChild> mCanvasChild;
  RefPtr<webgpu::WebGPUChild> mWebGPUChild;
  const uint32_t mId;
  bool mActive = true;

  /**
   * Hold TextureClients refs until end of their usages on host side.
   * It defer calling of TextureClient recycle callback.
   */
  std::unordered_map<uint64_t, RefPtr<layers::TextureClient>>
      mTexturesWaitingNotifyNotUsed;

  static MOZ_THREAD_LOCAL(CanvasManagerChild*) sLocalManager;
  static Atomic<uint32_t> sNextId;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_CanvasManagerChild_h__
