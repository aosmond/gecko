/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceImpl_h
#define mozilla_dom_FontFaceImpl_h

#include "mozilla/Attributes.h"
#include "nsISupportsImpl.h"

namespace mozilla {
namespace dom {

class FontFace;

class FontFaceImpl {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontFaceImpl)

 public:
  explicit FontFaceImpl(FontFace* aOwner);

  void Destroy();

  FontFace* GetOwner() const { return mOwner; }

 private:
  ~FontFaceImpl();

  FontFace* MOZ_NON_OWNING_REF mOwner;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_FontFaceImpl_h)
