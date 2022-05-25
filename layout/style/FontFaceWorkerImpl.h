/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceWorkerImpl_h
#define mozilla_dom_FontFaceWorkerImpl_h

#include "mozilla/dom/FontFaceImpl.h"
#include "mozilla/Mutex.h"

namespace mozilla::dom {

class FontFaceSetWorkerImpl;

class FontFaceWorkerImpl : public FontFaceImpl {
 public:
  explicit FontFaceWorkerImpl(FontFace* aOwner,
                              FontFaceSetWorkerImpl* aPrimarySet);

  RefPtr<FontFaceSetImpl> GetPrimaryFontFaceSet() const override;

  void Destroy() override;

 protected:
  ~FontFaceWorkerImpl() override;

  mutable Mutex mMutex;
};

}  // namespace mozilla::dom

#endif  // !defined(mozilla_dom_FontFaceWorkerImpl_h)
