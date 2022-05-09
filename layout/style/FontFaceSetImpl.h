/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSetImpl_h
#define mozilla_dom_FontFaceSetImpl_h

#include "gfxUserFontSet.h"
#include "mozilla/Attributes.h"
#include "nsISupportsImpl.h"

namespace mozilla {
namespace dom {

class FontFaceSet;

/**
 * Base class providing basic functionality for FontFaceSet which is subclassed
 * for the document on the main thread, and for workers.
 */
class FontFaceSetImpl : public nsISupports, public gfxUserFontSet {
  NS_DECL_THREADSAFE_ISUPPORTS

 public:
  virtual void Destroy();

  FontFaceSet* GetOwner() const { return mOwner; }

  gfxFontSrcPrincipal* GetStandardFontLoadPrincipal() const final {
    return mFontPrincipal;
  }

 protected:
  explicit FontFaceSetImpl(FontFaceSet* aOwner);
  virtual ~FontFaceSetImpl();

  FontFaceSet* MOZ_NON_OWNING_REF mOwner;

  RefPtr<gfxFontSrcPrincipal> mFontPrincipal;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_FontFaceSetImpl_h)
