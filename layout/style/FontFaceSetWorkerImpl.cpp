/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceSetWorkerImpl.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"

namespace mozilla {
namespace dom {

already_AddRefed<FontFaceSetWorkerImpl> FontFaceSetWorkerImpl::Create(
    FontFaceSet* aOwner) {
  RefPtr<FontFaceSetWorkerImpl> set;
  //auto set = MakeRefPtr<FontFaceSetWorkerImpl>(aOwner);

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
      workerPrivate, "FontFaceSetWorkerImpl", [set]() { set->Destroy(); });
  if (NS_WARN_IF(!workerRef)) {
    return nullptr;
  }

  MutexAutoLock lock(set->mMutex);
  set->mWorkerRef = new ThreadSafeWorkerRef(workerRef);
  set->mBypassCache = workerPrivate->ShouldBypassCache();
  set->mPrivateBrowsing = workerPrivate->UsePrivateBrowsing();
  return set.forget();
}

FontFaceSetWorkerImpl::FontFaceSetWorkerImpl(FontFaceSet* aOwner)
    : FontFaceSetImpl(aOwner), mMutex("FontFaceSetWorkerImpl") {}

FontFaceSetWorkerImpl::~FontFaceSetWorkerImpl() = default;

void FontFaceSetWorkerImpl::Destroy() {
  MutexAutoLock lock(mMutex);
  FontFaceSetImpl::Destroy();
  mWorkerRef = nullptr;
}

void FontFaceSetWorkerImpl::CreateFontPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  if (mFontPrincipal || !mWorkerRef) {
    return;
  }

  WorkerPrivate* workerPrivate = mWorkerRef->Private();
  mFontPrincipal = new gfxFontSrcPrincipal(
      workerPrivate->GetPrincipal(), workerPrivate->GetPartitionedPrincipal());
}

}  // namespace dom
}  // namespace mozilla
