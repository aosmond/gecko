/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSetWorkerImpl_h
#define mozilla_dom_FontFaceSetWorkerImpl_h

#include "mozilla/dom/FontFaceSetImpl.h"

namespace mozilla::dom {
class WorkerPrivate;

class FontFaceSetWorkerImpl final : public FontFaceSetImpl {
  NS_DECL_ISUPPORTS_INHERITED

 public:
  FontFaceSetWorkerImpl(FontFaceSet* aOwner, WorkerPrivate* aWorkerPrivate);

  void Initialize() override;
  void Destroy() override;

  // gfxUserFontSet

  nsPresContext* GetPresContext() const override;

  bool UpdateRules(const nsTArray<nsFontFaceRuleContainer>& aRules) override;

  RawServoFontFaceRule* FindRuleForEntry(gfxFontEntry* aFontEntry) override;

  /**
   * Notification method called by the nsPresContext to indicate that the
   * refresh driver ticked and flushed style and layout.
   * were just flushed.
   */
  void DidRefresh() override;

  void EnsureReady() override;

  bool Add(FontFaceImpl* aFontFace, ErrorResult& aRv) override;

  void FlushUserFontSet() override;
  void MarkUserFontSetDirty() override;

 private:
  ~FontFaceSetWorkerImpl() override;

  // search for @font-face rule that matches a userfont font entry
  RawServoFontFaceRule* FindRuleForUserFontEntry(
      gfxUserFontEntry* aUserFontEntry) override;

  void FindMatchingFontFaces(const nsTHashSet<FontFace*>& aMatchingFaces,
                             nsTArray<FontFace*>& aFontFaces) override;

  // Helper function for HasLoadingFontFaces.
  void UpdateHasLoadingFontFaces() override;

  /**
   * Returns whether there might be any pending font loads, which should cause
   * the mReady Promise not to be resolved yet.
   */
  bool MightHavePendingFontLoads() override;

#ifdef DEBUG
  bool HasRuleFontFace(FontFaceImpl* aFontFace);
#endif

  already_AddRefed<gfxFontSrcPrincipal> CreateStandardFontLoadPrincipal()
      const override;

  TimeStamp GetNavigationStartTimeStamp() override;
};

}  // namespace mozilla::dom

#endif  // !defined(mozilla_dom_FontFaceSetWorkerImpl_h)
