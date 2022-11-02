/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_MediaSourceHandle_h_
#define mozilla_dom_MediaSourceHandle_h_

#include "mozilla/dom/MediaSourceBinding.h"

class nsIGlobalObject;

namespace mozilla::dom {

class MediaSourceHandle final : public nsISupports, public nsWrapperCache {
 public:
  /** WebIDL Methods. */

  /** End WebIDL Methods. */

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(MediaSourceHandle)

  nsIGlobalObject* GetParentObject() const { return mParent; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  virtual ~MediaSourceHandle();

  explicit MediaSourceHandle(nsIGlobalObject* aGlobal);

  nsCOMPtr<nsIGlobalObject> mParent;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_MediaSourceHandle_h_ */
