/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FontFaceImpl.h"
#include "mozilla/ErrorResult.h"
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
  mBufferSource = new BufferSource(aBuffer, aLength);
  Load();
}

void FontFaceImpl::InitializeSourceURL(const nsACString& aURL) {
  IgnoredErrorResult rv;
  SetDescriptor(eCSSFontDesc_Src, aURL, rv);
  if (rv.Failed()) {
  }

  Load();
}

FontFaceLoadStatus FontFaceImpl::Load() { return mStatus; }

bool FontFaceImpl::SetDescriptor(nsCSSFontDesc aFontDesc,
                                 const nsACString& aValue, ErrorResult& aRv) {
  return false;
}

bool FontFaceImpl::SetDescriptors(const nsACString& aFamily,
                                  const FontFaceDescriptors& aDescriptors) {
  return false;
}

}  // namespace dom
}  // namespace mozilla
