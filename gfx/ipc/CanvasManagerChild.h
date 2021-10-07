/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_CanvasManagerChild_h__
#define _include_gfx_ipc_CanvasManagerChild_h__

#include "mozilla/gfx/PCanvasManagerChild.h"
#include "mozilla/StaticMutex.h"
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace dom {
class WeakWorkerRef;
class WorkerPrivate;
}  // namespace dom

namespace gfx {

class CanvasManagerChild final : public PCanvasManagerChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CanvasManagerChild, override);

  static CanvasManagerChild* Get();
  static void Shutdown();
  static bool CreateParent(
      mozilla::ipc::Endpoint<PCanvasManagerParent>&& aEndpoint);

  explicit CanvasManagerChild(RefPtr<mozilla::dom::WeakWorkerRef>&& aWorkerRef);
  void ActorDestroy(ActorDestroyReason aReason) override;

 private:
  static void DestroyLocal();

  ~CanvasManagerChild();

  RefPtr<mozilla::dom::WeakWorkerRef> mWorkerRef;

  static StaticMutex sManagersMutex;

  using ManagerTable =
      nsRefPtrHashtable<nsPtrHashKey<mozilla::dom::WorkerPrivate>,
                        CanvasManagerChild>;
  static ManagerTable sManagers;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_CanvasManagerChild_h__
