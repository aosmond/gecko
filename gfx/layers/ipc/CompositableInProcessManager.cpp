/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableInProcessManager.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla::layers {

std::map<std::pair<base::ProcessId, uint64_t>, RefPtr<WebRenderImageHost>>
    CompositableInProcessManager::sCompositables;

Atomic<uint64_t> CompositableInProcessManager::sNextHandle(1);

/* static */ void CompositableInProcessManager::Shutdown() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  sCompositables.clear();
}

/* static */ RefPtr<WebRenderImageHost> CompositableInProcessManager::Add(
    const CompositableHandle& aHandle, base::ProcessId aForPid,
    const TextureInfo& aTextureInfo) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_RELEASE_ASSERT(aHandle.Value());

  const auto key = std::pair(aForPid, aHandle.Value());
  if (sCompositables.find(key) != sCompositables.end()) {
    MOZ_ASSERT_UNREACHABLE("Duplicate handle!");
    return nullptr;
  }

  auto host = MakeRefPtr<WebRenderImageHost>(aTextureInfo);
  sCompositables[key] = host;
  return host;
}

/* static */ RefPtr<WebRenderImageHost> CompositableInProcessManager::Find(
    const CompositableHandle& aHandle, base::ProcessId aForPid) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  const auto key = std::pair(aForPid, aHandle.Value());
  const auto i = sCompositables.find(key);
  if (NS_WARN_IF(i == sCompositables.end())) {
    return nullptr;
  }

  return i->second;
}

/* static */ void CompositableInProcessManager::Release(
    const CompositableHandle& aHandle, base::ProcessId aForPid) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  const auto key = std::pair(aForPid, aHandle.Value());
  const auto i = sCompositables.find(key);
  if (NS_WARN_IF(i == sCompositables.end())) {
    return;
  }

  sCompositables.erase(i);
}

}  // namespace mozilla::layers
