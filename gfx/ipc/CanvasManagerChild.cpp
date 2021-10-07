/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasManagerChild.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorManagerChild.h"

using namespace mozilla::dom;
using namespace mozilla::layers;

namespace mozilla::gfx {

StaticMutex CanvasManagerChild::sManagersMutex;
CanvasManagerChild::ManagerTable CanvasManagerChild::sManagers;

CanvasManagerChild::CanvasManagerChild(RefPtr<WeakWorkerRef>&& aWorkerRef)
    : mWorkerRef(std::move(aWorkerRef)){};

CanvasManagerChild::~CanvasManagerChild() = default;

void CanvasManagerChild::ActorDestroy(ActorDestroyReason aReason) {
  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();

  StaticMutexAutoLock lock(sManagersMutex);
  sManagers.WithEntryHandle(worker, [self = RefPtr{this}](auto&& entry) {
    if (entry.HasEntry() && entry.Data() == self) {
      entry.Remove();
    }
  });
}

/* static */ void CanvasManagerChild::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  // The worker threads should destroy their own CanvasManagerChild instances
  // during their shutdown sequence. We just need to take care of the main
  // thread.
  DestroyLocal();

#ifdef DEBUG
  StaticMutexAutoLock lock(sManagersMutex);
  MOZ_ASSERT(sManagers.IsEmpty());
#endif
}

/* static */ bool CanvasManagerChild::CreateParent(
    ipc::Endpoint<PCanvasManagerParent>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (!manager || !manager->CanSend()) {
    return false;
  }

  return manager->SendInitCanvasManager(std::move(aEndpoint));
}

/* static */ void CanvasManagerChild::DestroyLocal() {
  // When the worker gets destroyed, we get a callback to tear down. If the
  // worker is nullptr, then we are destroying for the main thread.
  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_IF(!worker, NS_IsMainThread());

  RefPtr<CanvasManagerChild> manager;
  {
    StaticMutexAutoLock lock(sManagersMutex);
    sManagers.Remove(worker, getter_AddRefs(manager));
  }

  if (manager) {
    manager->Close();
    manager->mWorkerRef = nullptr;
  }
}

/* static */ CanvasManagerChild* CanvasManagerChild::Get() {
  StaticMutexAutoLock lock(sManagersMutex);

  // We are only used on the main thread, or on worker threads.
  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_IF(!worker, NS_IsMainThread());

  CanvasManagerChild* managerWeak = sManagers.GetWeak(worker);
  if (managerWeak && managerWeak->CanSend()) {
    return managerWeak;
  }

  ipc::Endpoint<PCanvasManagerParent> parentEndpoint;
  ipc::Endpoint<PCanvasManagerChild> childEndpoint;

  auto compositorPid = CompositorManagerChild::GetOtherPid();
  if (NS_WARN_IF(!compositorPid)) {
    return nullptr;
  }

  nsresult rv = PCanvasManager::CreateEndpoints(
      compositorPid, base::GetCurrentProcId(), &parentEndpoint, &childEndpoint);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // The weak reference will let us know when the worker is shutting down. This
  // will let us remove our entry from the hashtable and close the actor.
  RefPtr<WeakWorkerRef> workerRef;
  if (worker) {
    workerRef = WeakWorkerRef::Create(
        worker, []() { CanvasManagerChild::DestroyLocal(); });
  }

  auto manager = MakeRefPtr<CanvasManagerChild>(std::move(workerRef));
  if (!childEndpoint.Bind(manager)) {
    return nullptr;
  }

  // We can't talk to the compositor process directly from worker threads, but
  // the main thread can via CompositorManagerChild.
  if (worker) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "CanvasManagerChild::CreateParent",
        [parentEndpoint = std::move(parentEndpoint)]() {
          CreateParent(
              std::move(const_cast<ipc::Endpoint<PCanvasManagerParent>&&>(
                  parentEndpoint)));
        }));
  } else if (!CreateParent(std::move(parentEndpoint))) {
    return nullptr;
  }

  sManagers.InsertOrUpdate(worker, manager);
  return manager;
}

}  // namespace mozilla::gfx
