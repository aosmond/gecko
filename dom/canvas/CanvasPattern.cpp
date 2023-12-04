/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasPattern.h"

#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/gfx/CanvasManagerChild.h"

namespace mozilla::dom {
CanvasPattern::CanvasPattern(CanvasRenderingContext2D* aContext,
                             gfx::SourceSurface* aSurface, RepeatMode aRepeat,
                             nsIPrincipal* principalForSecurityCheck,
                             bool forceWriteOnly, bool CORSUsed)
    : mContext(aContext),
      mSurface(aSurface),
      mPrincipal(principalForSecurityCheck),
      mForceWriteOnly(forceWriteOnly),
      mCORSUsed(CORSUsed),
      mRepeat(aRepeat) {
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->AddShutdownObserver(this);
  }
}

CanvasPattern::~CanvasPattern() {
  if (auto* cm = gfx::CanvasManagerChild::MaybeGet()) {
    cm->RemoveShutdownObserver(this);
  }
}

void CanvasPattern::OnShutdown() {
  mPrincipal = nullptr;
  mSurface = nullptr;
  mContext = nullptr;
}

}  // namespace mozilla::dom
