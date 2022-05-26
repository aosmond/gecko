/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceImpl.h"
#include "mozilla/dom/FontFaceSetImpl.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_layout.h"
#include "gfxUserFontSet.h"

namespace mozilla {
namespace dom {

class FontFaceImpl::BufferSource final : public gfxFontFaceBufferSource {
 public:
  BufferSource(uint8_t* aBuffer, uint32_t aLength)
      : mBuffer(aBuffer), mLength(aLength) {}

  ~BufferSource() override {
    if (mBuffer) {
      free(mBuffer);
    }
  }

  void TakeBuffer(uint8_t*& aBuffer, uint32_t& aLength) override {
    aBuffer = mBuffer;
    aLength = mLength;
    mBuffer = nullptr;
    mLength = 0;
  }

 private:
  uint8_t* mBuffer;
  uint32_t mLength;
};

FontFaceImpl::FontFaceImpl(FontFace* aOwner, FontFaceSetImpl* aPrimarySet)
    : mOwner(aOwner), mPrimarySet(aPrimarySet) {}

FontFaceImpl::~FontFaceImpl() = default;

void FontFaceImpl::Destroy() {
  mPrimarySet = nullptr;
  mOwner = nullptr;
}

RefPtr<FontFaceSetImpl> FontFaceImpl::GetPrimaryFontFaceSet() const {
  return RefPtr{mPrimarySet};
}

void FontFaceImpl::InitializeSourceBuffer(uint8_t* aBuffer, uint32_t aLength) {
  MOZ_ASSERT(!mBufferSource);
  mSourceType = eSourceType_Buffer;
  mBufferSource = new BufferSource(aBuffer, aLength);
  Load();
}

void FontFaceImpl::InitializeSourceURL(const nsACString& aURL) {
  MOZ_ASSERT(!mBufferSource);
  mSourceType = eSourceType_URLs;

  IgnoredErrorResult rv;
  SetDescriptor(eCSSFontDesc_Src, aURL, rv);
  if (rv.Failed()) {
    // FIXME
  }

  Load();
}

FontFaceLoadStatus FontFaceImpl::Load() { return mStatus; }

bool FontFaceImpl::SetDescriptor(nsCSSFontDesc aFontDesc,
                                 const nsACString& aValue, ErrorResult& aRv) {
  MOZ_ASSERT(mRule);

  if (mSourceType == eSourceType_FontFaceRule) {
    MOZ_ASSERT_UNREACHABLE("SetDescriptor for FontFaceRule!");
    return false;
  }

  if (NS_WARN_IF(!mPrimarySet)) {
    return false;
  }

  RefPtr<URLExtraData> extraData = mPrimarySet->GetURLExtraData();
  if (NS_WARN_IF(!extraData)) {
    return false;
  }

  bool changed;
  if (!Servo_FontFaceRule_SetDescriptor(mRule, aFontDesc, &aValue, extraData,
                                        &changed)) {
    aRv.ThrowSyntaxError("Invalid font descriptor");
    return false;
  }

  if (!changed) {
    return false;
  }

  if (aFontDesc == eCSSFontDesc_UnicodeRange) {
    mUnicodeRangeDirty = true;
  }

  return true;
}

bool FontFaceImpl::SetDescriptors(const nsACString& aFamily,
                                  const FontFaceDescriptors& aDescriptors) {
  if (mSourceType == eSourceType_FontFaceRule || mRule) {
    MOZ_ASSERT_UNREACHABLE("SetDescriptors for FontFaceRule!");
    return false;
  }

  mRule = Servo_FontFaceRule_CreateEmpty().Consume();

  // Helper to call SetDescriptor and return true on success, false on failure.
  auto setDesc = [=](nsCSSFontDesc aDesc, const nsACString& aVal) -> bool {
    IgnoredErrorResult rv;
    SetDescriptor(aDesc, aVal, rv);
    return !rv.Failed();
  };

  // Parse all of the mDescriptors in aInitializer, which are the values
  // we got from the JS constructor.
  if (!setDesc(eCSSFontDesc_Family, aFamily) ||
      !setDesc(eCSSFontDesc_Style, aDescriptors.mStyle) ||
      !setDesc(eCSSFontDesc_Weight, aDescriptors.mWeight) ||
      !setDesc(eCSSFontDesc_Stretch, aDescriptors.mStretch) ||
      !setDesc(eCSSFontDesc_UnicodeRange, aDescriptors.mUnicodeRange) ||
      !setDesc(eCSSFontDesc_FontFeatureSettings,
               aDescriptors.mFeatureSettings) ||
      (StaticPrefs::layout_css_font_variations_enabled() &&
       !setDesc(eCSSFontDesc_FontVariationSettings,
                aDescriptors.mVariationSettings)) ||
      !setDesc(eCSSFontDesc_Display, aDescriptors.mDisplay) ||
      (StaticPrefs::layout_css_font_metrics_overrides_enabled() &&
       (!setDesc(eCSSFontDesc_AscentOverride, aDescriptors.mAscentOverride) ||
        !setDesc(eCSSFontDesc_DescentOverride, aDescriptors.mDescentOverride) ||
        !setDesc(eCSSFontDesc_LineGapOverride,
                 aDescriptors.mLineGapOverride))) ||
      (StaticPrefs::layout_css_size_adjust_enabled() &&
       !setDesc(eCSSFontDesc_SizeAdjust, aDescriptors.mSizeAdjust))) {
    // XXX Handle font-variant once we support it (bug 1055385).

    // If any of the descriptors failed to parse, none of them should be set
    // on the FontFace.
    mRule = Servo_FontFaceRule_CreateEmpty().Consume();

    // Reject(NS_ERROR_DOM_SYNTAX_ERR);

    // SetStatus(FontFaceLoadStatus::Error);
    return false;
  }

  return true;
}

void FontFaceImpl::Reject(nsresult aResult) {
  AssertIsMainThreadOrServoFontMetricsLocked();

  if (mLoaded) {
    DoReject(aResult);
  } else if (mLoadedRejection == NS_OK) {
    mLoadedRejection = aResult;
  }
}


}  // namespace dom
}  // namespace mozilla
