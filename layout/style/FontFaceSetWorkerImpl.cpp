/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFaceSetWorkerImpl.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;

#define LOG(args) \
  MOZ_LOG(gfxUserFontSet::GetUserFontsLog(), mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gfxUserFontSet::GetUserFontsLog(), LogLevel::Debug)

NS_IMPL_ISUPPORTS_INHERITED0(FontFaceSetWorkerImpl, FontFaceSetImpl);

FontFaceSetWorkerImpl::FontFaceSetWorkerImpl(FontFaceSet* aOwner)
    : FontFaceSetImpl(aOwner, nullptr) {}

FontFaceSetWorkerImpl::~FontFaceSetWorkerImpl() = default;

bool FontFaceSetWorkerImpl::Initialize(WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT(aWorkerPrivate);

  // Record the state of the "bypass cache" flags from the docshell now,
  // since we want to look at them from style worker threads, and we can
  // only get to the docshell through a weak pointer (which is only
  // possible on the main thread).
  //
  // In theory the load type of a docshell could change after the document
  // is loaded, but handling that doesn't seem too important.
  mBypassCache = aWorkerPrivate->ShouldBypassCache();

  // Same for the "private browsing" flag.
  mPrivateBrowsing = aWorkerPrivate->UsePrivateBrowsing();

  RefPtr<StrongWorkerRef> workerRef =
      StrongWorkerRef::Create(aWorkerPrivate, "FontFaceSetWorkerImpl",
                              [self = RefPtr{this}] { self->Destroy(); });
  if (NS_WARN_IF(!workerRef)) {
    return false;
  }

  mWorkerRef = new ThreadSafeWorkerRef(workerRef);
  return true;
}

void FontFaceSetWorkerImpl::Destroy() {
  mWorkerRef = nullptr;
  FontFaceSetImpl::Destroy();
}

nsPresContext* FontFaceSetWorkerImpl::GetPresContext() const { return nullptr; }

already_AddRefed<gfxFontSrcPrincipal>
FontFaceSetWorkerImpl::CreateStandardFontLoadPrincipal() const {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mWorkerRef) {
    return nullptr;
  }

  WorkerPrivate* workerPrivate = mWorkerRef->Private();
  return MakeAndAddRef<gfxFontSrcPrincipal>(
      workerPrivate->GetPrincipal(), workerPrivate->GetPartitionedPrincipal());
}

TimeStamp FontFaceSetWorkerImpl::GetNavigationStartTimeStamp() {
  if (!mWorkerRef) {
    return TimeStamp();
  }

  return mWorkerRef->Private()->CreationTimeStamp();
}

#undef LOG_ENABLED
#undef LOG
