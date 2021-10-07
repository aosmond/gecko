/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_OffscreenCanvasManagerChild_h__
#define _include_gfx_ipc_OffscreenCanvasManagerChild_h__

#include "mozilla/gfx/POffscreenCanvasManagerChild.h"

namespace mozilla {
namespace gfx {

class OffscreenCanvasManagerChild final : public POffscreenCanvasManagerChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OffscreenCanvasManagerChild, override);

  static OffscreenCanvasManagerChild* Get();

  OffscreenCanvasManagerChild();

 private:
  ~OffscreenCanvasManagerChild();
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_OffscreenCanvasManagerChild_h__
