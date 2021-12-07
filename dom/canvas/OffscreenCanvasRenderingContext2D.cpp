/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OffscreenCanvasRenderingContext2D.h"
#include "mozilla/dom/OffscreenCanvasRenderingContext2DBinding.h"
#include "nsIGlobalObject.h"
#include "OffscreenCanvas.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(OffscreenCanvasRenderingContext2D,
                                      mOffscreenCanvas)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(OffscreenCanvasRenderingContext2D)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(OffscreenCanvasRenderingContext2D)
NS_IMPL_CYCLE_COLLECTING_RELEASE(OffscreenCanvasRenderingContext2D)

OffscreenCanvasRenderingContext2D::OffscreenCanvasRenderingContext2D(
    OffscreenCanvas* aOffscreenCanvas)
    : mOffscreenCanvas(aOffscreenCanvas) {}

OffscreenCanvasRenderingContext2D::~OffscreenCanvasRenderingContext2D() =
    default;

nsIGlobalObject* OffscreenCanvasRenderingContext2D::GetParentObject() const {
  return mOffscreenCanvas->GetParentObject();
}

JSObject* OffscreenCanvasRenderingContext2D::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return OffscreenCanvasRenderingContext2D_Binding::Wrap(aCx, this,
                                                         aGivenProto);
}

void OffscreenCanvasRenderingContext2D::Commit() {}

}  // namespace dom
}  // namespace mozilla
