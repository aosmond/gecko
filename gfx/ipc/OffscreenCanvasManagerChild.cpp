/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasManagerChild.h"
#include "BackgroundChildImpl.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"

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

  nsresult rv;
  ipc::Endpoint<POffscreenCanvasManagerChild> endpoint;
  if (!backgroundActor->SendCreateOffscreenCanvasManager(&rv, &endpoint)) {
    return nullptr;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  auto manager = MakeRefPtr<OffscreenCanvasManagerChild>();
  if (!endpoint.Bind(manager)) {
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
