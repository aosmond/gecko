/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSetMainImpl_h
#define mozilla_dom_FontFaceSetMainImpl_h

#include "mozilla/dom/FontFaceSetImpl.h"
#include "nsICSSLoaderObserver.h"
#include "nsIDOMEventListener.h"

namespace mozilla {
namespace dom {

class Document;

/**
 * Subclass of FontFaceSetImpl designed to be used from the main thread in
 * conjunction with an owning Document.
 */
class FontFaceSetMainImpl final : public FontFaceSetImpl,
                                  public nsIDOMEventListener,
                                  public nsICSSLoaderObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMEVENTLISTENER

  explicit FontFaceSetMainImpl(FontFaceSet* aOwner, Document* aDocument);

  void Destroy() override;

  // nsICSSLoaderObserver
  NS_IMETHOD StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                              nsresult aStatus) override;

 private:
  ~FontFaceSetMainImpl() override;

  void CreateFontPrincipal();

  RefPtr<Document> mDocument;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_FontFaceSetImpl_h)
