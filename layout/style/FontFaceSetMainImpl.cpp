/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceSetMainImpl.h"

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS_INHERITED(FontFaceSetMainImpl, FontFaceSetImpl,
                            nsIDOMEventListener, nsICSSLoaderObserver)

FontFaceSetMainImpl::FontFaceSetMainImpl(FontFaceSet* aOwner)
    : FontFaceSetImpl(aOwner) {}

FontFaceSetMainImpl::~FontFaceSetMainImpl() = default;

void FontFaceSetMainImpl::Destroy() { FontFaceSetImpl::Destroy(); }

// nsIDOMEventListener

NS_IMETHODIMP FontFaceSetMainImpl::HandleEvent(Event* aEvent) { return NS_OK; }

// nsICSSLoaderObserver

NS_IMETHODIMP FontFaceSetMainImpl::StyleSheetLoaded(StyleSheet* aSheet,
                                                    bool aWasDeferred,
                                                    nsresult aStatus) {
  return NS_OK;
}

}  // namespace dom
}  // namespace mozilla
