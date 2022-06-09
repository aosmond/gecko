/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFaceSetWorkerImpl.h"
#include "FontPreloader.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "nsFontFaceLoader.h"
#include "nsINetworkPredictor.h"

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

nsresult FontFaceSetWorkerImpl::StartLoad(gfxUserFontEntry* aUserFontEntry,
                                          uint32_t aSrcIndex) {
  nsresult rv;

  nsCOMPtr<nsIStreamLoader> streamLoader;

  const gfxFontFaceSrc& src = aUserFontEntry->SourceAt(aSrcIndex);

  nsCOMPtr<nsILoadGroup> loadGroup(mWorkerRef->Private()->GetLoadGroup());
  nsCOMPtr<nsIChannel> channel;
  rv = FontPreloader::BuildChannel(
      getter_AddRefs(channel), src.mURI->get(), CORS_ANONYMOUS,
      dom::ReferrerPolicy::_empty /* not used */, aUserFontEntry, &src,
      mWorkerRef->Private(), loadGroup, nullptr, false);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsFontFaceLoader> fontLoader =
      new nsFontFaceLoader(aUserFontEntry, aSrcIndex, this, channel);

  if (LOG_ENABLED()) {
    nsCOMPtr<nsIURI> referrer =
        src.mReferrerInfo ? src.mReferrerInfo->GetOriginalReferrer() : nullptr;
    LOG(("userfonts (%p) download start - font uri: (%s) referrer uri: (%s)\n",
         fontLoader.get(), src.mURI->GetSpecOrDefault().get(),
         referrer ? referrer->GetSpecOrDefault().get() : ""));
  }

  rv = NS_NewStreamLoader(getter_AddRefs(streamLoader), fontLoader, fontLoader);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = channel->AsyncOpen(streamLoader);
  if (NS_FAILED(rv)) {
    fontLoader->DropChannel();  // explicitly need to break ref cycle
  }

  mLoaders.PutEntry(fontLoader);

  net::PredictorLearn(src.mURI->get(), mWorkerRef->Private()->GetBaseURI(),
                      nsINetworkPredictor::LEARN_LOAD_SUBRESOURCE, loadGroup);

  if (NS_SUCCEEDED(rv)) {
    fontLoader->StartedLoading(streamLoader);
    // let the font entry remember the loader, in case we need to cancel it
    aUserFontEntry->SetLoader(fontLoader);
  }

  return rv;
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

already_AddRefed<URLExtraData> FontFaceSetWorkerImpl::GetURLExtraData() {
  if (!mWorkerRef) {
    return nullptr;
  }

  return RefPtr{mWorkerRef->Private()->GetURLExtraData()}.forget();
}

#undef LOG_ENABLED
#undef LOG
