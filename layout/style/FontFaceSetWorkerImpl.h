/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSetWorkerImpl_h
#define mozilla_dom_FontFaceSetWorkerImpl_h

#include "mozilla/dom/FontFaceSetImpl.h"
#include "mozilla/Mutex.h"

namespace mozilla {
namespace dom {

class ThreadSafeWorkerRef;

/**
 * Subclass of FontFaceSetImpl designed to be used from a DOM worker thread.
 */
class FontFaceSetWorkerImpl final : public FontFaceSetImpl {
 public:
  static already_AddRefed<FontFaceSetWorkerImpl> Create(FontFaceSet* aOwner);

  explicit FontFaceSetWorkerImpl(FontFaceSet* aOwner);

  void Destroy() override;

 private:
  ~FontFaceSetWorkerImpl() override;

  void CreateFontPrincipal();

  Mutex mMutex;
  RefPtr<ThreadSafeWorkerRef> mWorkerRef GUARDED_BY(mMutex);
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_FontFaceSetWorkerImpl_h)
