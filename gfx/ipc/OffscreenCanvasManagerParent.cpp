/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasManagerParent.h"
#include "mozilla/dom/WebGLParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsIThread.h"

namespace mozilla::gfx {

OffscreenCanvasManagerParent::ManagerMap
    OffscreenCanvasManagerParent::sManagers;

/* static */ void OffscreenCanvasManagerParent::Init(
    Endpoint<POffscreenCanvasManagerParent>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());

  auto* renderThread = wr::RenderThread::Get();
  MOZ_ASSERT(renderThread);
  nsCOMPtr<nsIThread> owningThread = renderThread->GetRenderThread();
  MOZ_ASSERT(owningThread);

  auto manager = MakeRefPtr<OffscreenCanvasManagerParent>();
  owningThread->Dispatch(
      NewRunnableMethod<Endpoint<POffscreenCanvasManagerParent>&&>(
          "OffscreenCanvasManagerParent::Bind", manager,
          &OffscreenCanvasManagerParent::Bind, std::move(aEndpoint)));
}

/* static */ void OffscreenCanvasManagerParent::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  auto* renderThread = wr::RenderThread::Get();
  MOZ_ASSERT(renderThread);
  nsCOMPtr<nsIThread> owningThread = renderThread->GetRenderThread();
  MOZ_ASSERT(owningThread);

  owningThread->Dispatch(
      NS_NewRunnableFunction(
          "OffscreenCanvasManagerParent::Shutdown",
          []() -> void { OffscreenCanvasManagerParent::ShutdownInternal(); }),
      NS_DISPATCH_SYNC);
}

/* static */ void OffscreenCanvasManagerParent::ShutdownInternal() {
  nsTArray<RefPtr<OffscreenCanvasManagerParent>> actors;
  // Do a copy since Close will remove the entry from the map.
  for (const auto& iter : sManagers) {
    actors.AppendElement(iter.second);
  }

  for (auto const& actor : actors) {
    actor->Close();
  }
}

OffscreenCanvasManagerParent::OffscreenCanvasManagerParent() = default;
OffscreenCanvasManagerParent::~OffscreenCanvasManagerParent() = default;

void OffscreenCanvasManagerParent::Bind(
    Endpoint<POffscreenCanvasManagerParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    NS_WARNING("Failed to bind OffscreenCanvasManagerParent!");
    return;
  }

  // If the child process ID was reused by the OS before the
  // OffscreenCanvasManagerParent object was destroyed, we need to clean it up.
  ManagerMap::const_iterator i = sManagers.find(OtherPid());
  if (i != sManagers.end()) {
    RefPtr<OffscreenCanvasManagerParent> oldActor = i->second;
    oldActor->Close();
  }

  sManagers[OtherPid()] = this;
}

void OffscreenCanvasManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  sManagers.erase(OtherPid());
}

already_AddRefed<dom::PWebGLParent>
OffscreenCanvasManagerParent::AllocPWebGLParent() {
  printf_stderr(
      "[AO] Creating PWebGLParent via OffscreenCanvasManagerParent!!!!!\n");
  return MakeAndAddRef<dom::WebGLParent>();
}

}  // namespace mozilla::gfx
