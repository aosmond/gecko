/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ImageTrack_h
#define mozilla_dom_ImageTrack_h

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/NotNull.h"
#include "mozilla/dom/ImageDecoderBinding.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla::dom {
class ImageTrackList;

class ImageTrack final : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ImageTrack)

 public:
  ImageTrack(ImageTrackList* aTrackList, int32_t aIndex, bool aSelected,
             bool aAnimated, uint32_t aFrameCount, float aRepetitionCount);

 protected:
  ~ImageTrack();

 public:
  nsIGlobalObject* GetParentObject() const { return mParent; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  bool Animated() const { return mAnimated; }

  uint32_t FrameCount() const { return mFrameCount; }

  float RepetitionCount() const { return mRepetitionCount; }

  bool Selected() const { return mSelected; }

  void SetSelected(bool aSelected);

  void ClearSelected() { mSelected = false; }
  void MarkSelected() { mSelected = true; }

 private:
  // ImageTrack can run on either main thread or worker thread.
  void AssertIsOnOwningThread() const { NS_ASSERT_OWNINGTHREAD(ImageTrack); }

  nsCOMPtr<nsIGlobalObject> mParent;
  RefPtr<ImageTrackList> mTrackList;
  int32_t mIndex = 0;
  float mRepetitionCount = 0.0f;
  uint32_t mFrameCount = 0;
  bool mAnimated = false;
  bool mSelected = false;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ImageTrack_h
