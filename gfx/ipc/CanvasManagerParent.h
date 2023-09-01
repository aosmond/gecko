/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_CanvasManagerParent_h__
#define _include_gfx_ipc_CanvasManagerParent_h__

#include "mozilla/gfx/PCanvasManagerParent.h"
#include "mozilla/StaticMonitor.h"
#include "mozilla/UniquePtr.h"
#include "nsHashtablesFwd.h"
#include <unordered_map>

namespace mozilla {
namespace layers {
class SurfaceDescriptor;
}

namespace gfx {

class CanvasManagerParent final : public PCanvasManagerParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CanvasManagerParent, override);

  static void Init(Endpoint<PCanvasManagerParent>&& aEndpoint);

  static void Shutdown();

  static void DisableRemoteCanvas();

  static void AddReplayTexture(base::ProcessId aOtherPid, int64_t aTextureId,
                               layers::TextureData* aTextureData);

  static void RemoveReplayTexture(base::ProcessId aOtherPid,
                                  int64_t aTextureId);

  static UniquePtr<layers::SurfaceDescriptor> WaitForReplayTexture(
      base::ProcessId aOtherPid, int64_t aTextureId);

  CanvasManagerParent();

  void Bind(Endpoint<PCanvasManagerParent>&& aEndpoint);
  void ActorDestroy(ActorDestroyReason aWhy) override;

  already_AddRefed<PWebGLParent> AllocPWebGLParent();
  already_AddRefed<PWebGPUParent> AllocPWebGPUParent();
  already_AddRefed<PCanvasParent> AllocPCanvasParent();

  mozilla::ipc::IPCResult RecvInitialize(const uint32_t& aId);
  mozilla::ipc::IPCResult RecvGetSnapshot(
      const uint32_t& aManagerId, const int32_t& aProtocolId,
      const Maybe<RemoteTextureOwnerId>& aOwnerId,
      webgl::FrontBufferSnapshotIpc* aResult);

 private:
  static void ShutdownInternal();
  static void DisableRemoteCanvasInternal();

  ~CanvasManagerParent() override;

  uint32_t mId = 0;

  using ManagerSet = nsTHashSet<CanvasManagerParent*>;
  static ManagerSet sManagers;

  struct ReplayTextureKey {
    base::ProcessId mOtherPid;
    int64_t mId;

    // Implement some operators so this class can be used as a key in
    // stdlib classes.
    bool operator<(const ReplayTextureKey& aOther) const {
      return mOtherPid < aOther.mOtherPid || mId < aOther.mId;
    }

    bool operator==(const ReplayTextureKey& aOther) const {
      return mOtherPid == aOther.mOtherPid && mId == aOther.mId;
    }

    bool operator!=(const ReplayTextureKey& aOther) const {
      return !(*this == aOther);
    }

    // Helper struct that allow this class to be used as a key in
    // std::unordered_map like so:
    //   std::unordered_map<ReplayTextureKey, ValueType,
    //                      ReplayTextureKey::HashFn> myMap;
    struct HashFn {
      std::size_t operator()(const ReplayTextureKey& aKey) const {
        return std::hash<int64_t>{}(static_cast<int64_t>(aKey.mOtherPid)
                                    << 32) ^
               std::hash<int64_t>{}(aKey.mId);
      }
    };
  };

  using ReplayTextureMap =
      std::unordered_map<ReplayTextureKey, UniquePtr<layers::SurfaceDescriptor>,
                         ReplayTextureKey::HashFn>;
  static StaticMonitor sReplayTexturesMonitor;
  static ReplayTextureMap sReplayTextures
      MOZ_GUARDED_BY(sReplayTexturesMonitor);
  static bool sReplayTexturesEnabled MOZ_GUARDED_BY(sReplayTexturesMonitor);
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_CanvasManagerParent_h__
