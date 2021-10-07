/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_OffscreenCanvasManagerParent_h__
#define _include_gfx_ipc_OffscreenCanvasManagerParent_h__

#include "mozilla/gfx/POffscreenCanvasManagerParent.h"
#include <map>

namespace mozilla {
namespace gfx {

class OffscreenCanvasManagerParent final
    : public POffscreenCanvasManagerParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OffscreenCanvasManagerParent, override);

  static void Init(Endpoint<POffscreenCanvasManagerParent>&& aEndpoint);

  static void Shutdown();

  OffscreenCanvasManagerParent();

  void Bind(Endpoint<POffscreenCanvasManagerParent>&& aEndpoint);
  void ActorDestroy(ActorDestroyReason aWhy) override;
  already_AddRefed<PWebGLParent> AllocPWebGLParent();

 private:
  static void ShutdownInternal();

  ~OffscreenCanvasManagerParent();

  using ManagerMap = std::map<base::ProcessId, OffscreenCanvasManagerParent*>;
  static ManagerMap sManagers;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_OffscreenCanvasManagerParent_h__
