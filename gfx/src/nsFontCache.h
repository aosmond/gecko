/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _NS_FONTCACHE_H_
#define _NS_FONTCACHE_H_

#include <stdint.h>
#include <sys/types.h>
#include "mozilla/RefPtr.h"
#include "mozilla/RecursiveMutex.h"
#include "nsCOMPtr.h"
#include "nsFontMetrics.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

class gfxUserFontSet;
class nsAtom;
class nsPresContext;
struct nsFont;

class nsFontCache final : public nsIObserver {
 public:
  nsFontCache() : mContext(nullptr), mMutex("nsFontCache::mMutex") {}

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  void Init(nsPresContext* aContext);
  void Destroy();

  already_AddRefed<nsFontMetrics> GetMetricsFor(
      const nsFont& aFont, const nsFontMetrics::Params& aParams);

  void FontMetricsDeleted(const nsFontMetrics* aFontMetrics);
  void Compact();

  // Flush aFlushCount oldest entries, or all if aPartial is false.
  void Flush(bool aPartial = false) {
    mozilla::RecursiveMutexAutoLock lock(mMutex);
    FlushLocked(aPartial);
  }

  void UpdateUserFonts(gfxUserFontSet* aUserFontSet);

 protected:
  // If the array of cached entries is about to exceed this threshold,
  // we'll discard the oldest ones so as to keep the size reasonable.
  // In practice, the great majority of cache hits are among the last
  // few entries; keeping thousands of older entries becomes counter-
  // productive because it can then take too long to scan the cache.
  static const int32_t kMaxCacheEntries = 128;

  ~nsFontCache() = default;

  nsFontMetrics* LookupLocked(nsAtom* aLanguage, const nsFont& aFont,
                              const nsFontMetrics::Params& aParams)
      MOZ_REQUIRES(mMutex);

  void FlushLocked(bool aPartial) MOZ_REQUIRES(mMutex);

  nsPresContext* mContext;  // owner
  RefPtr<nsAtom> mLocaleLanguage;

  mozilla::RecursiveMutex mMutex;

  // We may not flush older entries immediately the array reaches
  // kMaxCacheEntries length, because this usually happens on a stylo
  // thread where we can't safely delete metrics objects. So we allocate an
  // oversized autoarray buffer here, so that we're unlikely to overflow
  // it and need separate heap allocation before the flush happens on the
  // main thread.
  AutoTArray<nsFontMetrics*, kMaxCacheEntries * 2> mFontMetrics
      MOZ_GUARDED_BY(mMutex);

  bool mFlushPending MOZ_GUARDED_BY(mMutex) = false;

  // Generation counter for when we insert new entries into mFontMetrics.
  uint32_t mGeneration MOZ_GUARDED_BY(mMutex) = 0;

  class FlushFontMetricsTask : public mozilla::Runnable {
   public:
    explicit FlushFontMetricsTask(nsFontCache* aCache)
        : mozilla::Runnable("FlushFontMetricsTask"), mCache(aCache) {}
    NS_IMETHOD Run() override {
      // Partially flush the cache, leaving the kMaxCacheEntries/2 most
      // recent entries.
      mCache->Flush(/* aPartial */ true);
      return NS_OK;
    }

   private:
    RefPtr<nsFontCache> mCache;
  };
};

#endif /* _NS_FONTCACHE_H_ */
