/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFontCache.h"

#include "gfxTextRun.h"
#include "mozilla/Services.h"
#include "nsCRT.h"

using mozilla::RecursiveMutexAutoLock;
using mozilla::services::GetObserverService;

NS_IMPL_ISUPPORTS(nsFontCache, nsIObserver)

// The Init and Destroy methods are necessary because it's not
// safe to call AddObserver from a constructor or RemoveObserver
// from a destructor.  That should be fixed.
void nsFontCache::Init(nsPresContext* aContext) {
  mContext = aContext;
  // register as a memory-pressure observer to free font resources
  // in low-memory situations.
  nsCOMPtr<nsIObserverService> obs = GetObserverService();
  if (obs) {
    obs->AddObserver(this, "memory-pressure", false);
  }

  mLocaleLanguage = nsLanguageAtomService::GetService()->GetLocaleLanguage();
  if (!mLocaleLanguage) {
    mLocaleLanguage = NS_Atomize("x-western");
  }
}

void nsFontCache::Destroy() {
  nsCOMPtr<nsIObserverService> obs = GetObserverService();
  if (obs) {
    obs->RemoveObserver(this, "memory-pressure");
  }
  Flush();
}

NS_IMETHODIMP
nsFontCache::Observe(nsISupports*, const char* aTopic, const char16_t*) {
  if (!nsCRT::strcmp(aTopic, "memory-pressure")) {
    Compact();
  }
  return NS_OK;
}

nsFontMetrics* nsFontCache::LookupLocked(nsAtom* aLanguage, const nsFont& aFont,
                                         const nsFontMetrics::Params& aParams) {
  // start from the end, which is where we put the most-recent-used element
  const int32_t n = mFontMetrics.Length() - 1;
  for (int32_t i = n; i >= 0; --i) {
    nsFontMetrics* fm = mFontMetrics[i];
    if (fm->Font().Equals(aFont) &&
        fm->GetUserFontSet() == aParams.userFontSet &&
        fm->Language() == aLanguage &&
        fm->Orientation() == aParams.orientation &&
        fm->ExplicitLanguage() == aParams.explicitLanguage) {
      if (i != n) {
        // promote it to the end of the cache
        mFontMetrics.RemoveElementAt(i);
        mFontMetrics.AppendElement(fm);
      }
      return fm;
    }
  }
  return nullptr;
}

already_AddRefed<nsFontMetrics> nsFontCache::GetMetricsFor(
    const nsFont& aFont, const nsFontMetrics::Params& aParams) {
  nsAtom* language = aParams.language && !aParams.language->IsEmpty()
                         ? aParams.language
                         : mLocaleLanguage.get();

  // First check our cache
  // start from the end, which is where we put the most-recent-used element
  mMutex.Lock();
  if (nsFontMetrics* fontMetrics = LookupLocked(language, aFont, aParams)) {
    RefPtr<nsFontMetrics> fm = fontMetrics;
    mMutex.Unlock();
    fontMetrics->GetThebesFontGroup()->UpdateUserFonts();
    return fm.forget();
  }

  // It's not in the cache. Get font metrics and then cache them.
  // If the cache has reached its size limit, drop the older half of the
  // entries; but if we're on a stylo thread (the usual case), we have
  // to post a task back to the main thread to do the flush.
  if (!mFlushPending && mFontMetrics.Length() >= kMaxCacheEntries) {
    if (NS_IsMainThread()) {
      FlushLocked(/* aPartial */ true);
    } else {
      mFlushPending = true;
      nsCOMPtr<nsIRunnable> flushTask = new FlushFontMetricsTask(this);
      MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(flushTask));
    }
  }

  // Create font metrics outside the lock because it will acquire other locks.
  uint32_t generation = mGeneration;
  mMutex.Unlock();
  nsFontMetrics::Params params = aParams;
  params.language = language;
  RefPtr<nsFontMetrics> fm = new nsFontMetrics(aFont, params, mContext);
  mMutex.Lock();

  // Recheck the cache if we raced against another thread to create a new entry.
  // If we have, destroy our unused metrics. We check the generation counter
  // because the scan itself may be expensive.
  if (generation != mGeneration) {
    if (nsFontMetrics* fontMetrics = LookupLocked(language, aFont, aParams)) {
      fm->Destroy();
      fm = fontMetrics;
      mMutex.Unlock();
      fontMetrics->GetThebesFontGroup()->UpdateUserFonts();
      return fm.forget();
    }
  }

  // the mFontMetrics list has the "head" at the end, because append
  // is cheaper than insert
  mFontMetrics.AppendElement(do_AddRef(fm).take());
  ++mGeneration;
  mMutex.Unlock();
  return fm.forget();
}

void nsFontCache::UpdateUserFonts(gfxUserFontSet* aUserFontSet) {
  AutoTArray<RefPtr<gfxFontGroup>, kMaxCacheEntries> fontGroups;

  {
    RecursiveMutexAutoLock lock(mMutex);
    for (nsFontMetrics* fm : mFontMetrics) {
      gfxFontGroup* fg = fm->GetThebesFontGroup();
      if (fg->GetUserFontSet() == aUserFontSet) {
        fontGroups.AppendElement(fg);
      }
    }
  }

  for (gfxFontGroup* fg : fontGroups) {
    fg->UpdateUserFonts();
  }
}

void nsFontCache::FontMetricsDeleted(const nsFontMetrics* aFontMetrics) {
  RecursiveMutexAutoLock lock(mMutex);
  mFontMetrics.RemoveElement(aFontMetrics);
}

void nsFontCache::Compact() {
  RecursiveMutexAutoLock lock(mMutex);
  // Need to loop backward because the running element can be removed on
  // the way
  for (int32_t i = mFontMetrics.Length() - 1; i >= 0; --i) {
    nsFontMetrics* fm = mFontMetrics[i];
    nsFontMetrics* oldfm = fm;
    // Destroy() isn't here because we want our device context to be
    // notified
    NS_RELEASE(fm);  // this will reset fm to nullptr
    // if the font is really gone, it would have called back in
    // FontMetricsDeleted() and would have removed itself
    if (mFontMetrics.IndexOf(oldfm) != mFontMetrics.NoIndex) {
      // nope, the font is still there, so let's hold onto it too
      NS_ADDREF(oldfm);
    }
  }
}

// Flush aFlushCount oldest entries, or all if aPartial is false.
void nsFontCache::FlushLocked(bool aPartial) {
  mFlushPending = false;

  int32_t length = mFontMetrics.Length();
  int32_t n = aPartial
                  ? std::min<int32_t>(length - kMaxCacheEntries / 2, length)
                  : length;
  for (int32_t i = n - 1; i >= 0; --i) {
    nsFontMetrics* fm = mFontMetrics[i];
    // Destroy() will unhook our device context from the fm so that we
    // won't waste time in triggering the notification of
    // FontMetricsDeleted() in the subsequent release
    fm->Destroy();
    NS_RELEASE(fm);
  }
  mFontMetrics.RemoveElementsAt(0, n);
}
