/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageDecoder.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageDecoder, mParent, mTracks,
                                      mCompletePromise)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageDecoder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageDecoder)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageDecoder)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageDecoder::ImageDecoder(nsIGlobalObject* aParent) : mParent(aParent) {}

ImageDecoder::~ImageDecoder() = default;

JSObject* ImageDecoder::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageDecoder_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */ already_AddRefed<ImageDecoder> ImageDecoder::Constructor(
    const GlobalObject& aGlobal, const ImageDecoderInit& aInit,
    ErrorResult& aRv) {
  return nullptr;
}

/* static */ already_AddRefed<Promise> ImageDecoder::IsTypeSupported(
    const GlobalObject& aGlobal, const nsAString& aType) {
  return nullptr;
}

void ImageDecoder::GetType(nsAString& aType) const {}

already_AddRefed<Promise> ImageDecoder::Decode(
    const ImageDecodeOptions& aOptions) {
  return nullptr;
}

void ImageDecoder::Reset() {}

void ImageDecoder::Close() {}

}  // namespace mozilla::dom
