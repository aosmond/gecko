/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceWorkerImpl.h"
#include "mozilla/dom/FontFaceSetWorkerImpl.h"

namespace mozilla::dom {

FontFaceWorkerImpl::FontFaceWorkerImpl(FontFace* aOwner,
                                       FontFaceSetWorkerImpl* aPrimarySet)
    : FontFaceImpl(aOwner),
      mMutex("FontFaceWorkerImpl"),
      mPrimarySet(aPrimarySet) {}

FontFaceWorkerImpl::~FontFaceWorkerImpl() = default;

void FontFaceWorkerImpl::Destroy() {
  MutexAutoLock lock(mMutex);
  FontFaceImpl::Destroy();
  mPrimarySet = nullptr;
}

RefPtr<FontFaceSetImpl> FontFaceWorkerImpl::GetPrimaryFontFaceSet() const {
  MutexAutoLock lock(mMutex);
  return RefPtr{mPrimarySet};
}

}  // namespace mozilla::dom
