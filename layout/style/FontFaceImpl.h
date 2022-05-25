/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceImpl_h
#define mozilla_dom_FontFaceImpl_h

#include "mozilla/Attributes.h"
#include "mozilla/dom/FontFaceBinding.h"
#include "nsCSSPropertyID.h"
#include "nsISupportsImpl.h"
#include "nsString.h"

namespace mozilla {

class ErrorResult;

namespace dom {

class FontFace;
class FontFaceSetImpl;

class FontFaceImpl {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FontFaceImpl)

 public:
  explicit FontFaceImpl(FontFace* aOwner, FontFaceSetImpl* aPrimarySet);

  virtual void InitializeSourceBuffer(uint8_t* aBuffer, uint32_t aLength);
  virtual void InitializeSourceURL(const nsACString& aURL);

  virtual FontFaceLoadStatus Load();

  virtual void Destroy();

  virtual RefPtr<FontFaceSetImpl> GetPrimaryFontFaceSet() const;

  virtual bool SetDescriptor(nsCSSFontDesc aFontDesc, const nsACString& aValue,
                             ErrorResult& aRv);

  virtual bool SetDescriptors(const nsACString& aFamily,
                              const FontFaceDescriptors& aDescriptors);

  FontFace* GetOwner() const { return mOwner; }

  FontFaceLoadStatus GetStatus() const { return mStatus; }

 protected:
  virtual ~FontFaceImpl();

  class BufferSource;

  FontFace* MOZ_NON_OWNING_REF mOwner;
  RefPtr<FontFaceSetImpl> mPrimarySet;
  RefPtr<BufferSource> mBufferSource;
  FontFaceLoadStatus mStatus = FontFaceLoadStatus::Unloaded;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_FontFaceImpl_h)
