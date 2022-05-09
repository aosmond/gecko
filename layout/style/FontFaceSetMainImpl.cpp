/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceSetMainImpl.h"
#include "mozilla/dom/Document.h"
#include "nsContentUtils.h"
#include "nsILoadContext.h"

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS_INHERITED(FontFaceSetMainImpl, FontFaceSetImpl,
                            nsIDOMEventListener, nsICSSLoaderObserver)

FontFaceSetMainImpl::FontFaceSetMainImpl(FontFaceSet* aOwner,
                                         Document* aDocument)
    : FontFaceSetImpl(aOwner), mDocument(aDocument) {
  // Record the state of the "bypass cache" flags from the docshell now,
  // since we want to look at them from style worker threads, and we can
  // only get to the docshell through a weak pointer (which is only
  // possible on the main thread).
  //
  // In theory the load type of a docshell could change after the document
  // is loaded, but handling that doesn't seem too important.
  mBypassCache = nsContentUtils::ShouldBypassCache(mDocument);

  // Same for the "private browsing" flag.
  if (nsCOMPtr<nsILoadContext> loadContext = mDocument->GetLoadContext()) {
    mPrivateBrowsing = loadContext->UsePrivateBrowsing();
  }
}

FontFaceSetMainImpl::~FontFaceSetMainImpl() = default;

void FontFaceSetMainImpl::Destroy() {
  FontFaceSetImpl::Destroy();
  mDocument = nullptr;
}

void FontFaceSetMainImpl::CreateFontPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mFontPrincipal || !mDocument) {
    return;
  }

  mFontPrincipal = new gfxFontSrcPrincipal(mDocument->NodePrincipal(),
                                           mDocument->PartitionedPrincipal());
}

// gfxUserFontSet

nsPresContext* FontFaceSetMainImpl::GetPresContext() const {
  if (mDocument) {
    return mDocument->GetPresContext();
  }
  return nullptr;
}

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
