/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasManagerChild.h"
#include "BackgroundChildImpl.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorManagerChild.h"

namespace mozilla::gfx {

OffscreenCanvasManagerChild::OffscreenCanvasManagerChild() = default;
OffscreenCanvasManagerChild::~OffscreenCanvasManagerChild() = default;

/* static */ OffscreenCanvasManagerChild* OffscreenCanvasManagerChild::Get() {
  ipc::BackgroundChildImpl::ThreadLocal* threadLocal =
      ipc::BackgroundChildImpl::GetThreadLocalForCurrentThread();
  if (threadLocal && threadLocal->mOffscreenCanvasManager) {
    return threadLocal->mOffscreenCanvasManager;
  }

  ipc::PBackgroundChild* backgroundActor =
      ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return nullptr;
  }

  ipc::Endpoint<POffscreenCanvasManagerParent> parentEndpoint;
  ipc::Endpoint<POffscreenCanvasManagerChild> childEndpoint;

  auto compositorPid = layers::CompositorManagerChild::GetOtherPid();
  if (NS_WARN_IF(!compositorPid)) {
    return nullptr;
  }

  nsresult rv = POffscreenCanvasManager::CreateEndpoints(
      compositorPid, base::GetCurrentProcId(), &parentEndpoint, &childEndpoint);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  if (!backgroundActor->SendCreateOffscreenCanvasManager(
          std::move(parentEndpoint))) {
    return nullptr;
  }

  auto manager = MakeRefPtr<OffscreenCanvasManagerChild>();
  if (!childEndpoint.Bind(manager)) {
    return nullptr;
  }

  if (!threadLocal) {
    threadLocal = ipc::BackgroundChildImpl::GetThreadLocalForCurrentThread();
    MOZ_ASSERT(threadLocal);
  }

  threadLocal->mOffscreenCanvasManager = std::move(manager);
  return threadLocal->mOffscreenCanvasManager;
}

}  // namespace mozilla::gfx
