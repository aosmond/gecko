/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ImageDecoder_h
#define mozilla_dom_ImageDecoder_h

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/NotNull.h"
#include "mozilla/dom/ImageDecoderBinding.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla {
namespace image {
class AnonymousDecoder;
class SourceBuffer;
enum class DecoderType;
enum class SurfaceFlags : uint8_t;
struct DecodeFramesResult;
}

namespace dom {
class Promise;
class VideoFrame;

class ImageDecoder final : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ImageDecoder)

 public:
  ImageDecoder(nsCOMPtr<nsIGlobalObject>&& aParent, const nsAString& aType);

 public:
  nsIGlobalObject* GetParentObject() const { return mParent; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<ImageDecoder> Constructor(
      const GlobalObject& aGlobal, const ImageDecoderInit& aInit,
      ErrorResult& aRv);

  static already_AddRefed<Promise> IsTypeSupported(const GlobalObject& aGlobal,
                                                   const nsAString& aType,
                                                   ErrorResult& aRv);

  void GetType(nsAString& aType) const;

  bool Complete() const { return mComplete; }

  Promise* Completed() const { return mCompletePromise; }

  ImageTrackList* Tracks() const { return mTracks; }

  already_AddRefed<Promise> Decode(const ImageDecodeOptions& aOptions,
                                   ErrorResult& aRv);

  void Reset();

  void Close();

 private:
  ~ImageDecoder();

  struct OutstandingDecode {
    RefPtr<Promise> mPromise;
    uint32_t mFrameIndex;
  };

  // VideoFrame can run on either main thread or worker thread.
  void AssertIsOnOwningThread() const { NS_ASSERT_OWNINGTHREAD(ImageDecoder); }

  void Initialize(const ImageDecoderInit& aInit, const uint8_t* aData,
                  size_t aLength, ErrorResult& aRv);

  void OnSourceBufferReady(image::SourceBuffer* aSourceBuffer,
                           image::DecoderType aType,
                           image::SurfaceFlags aSurfaceFlags);

  void OnDecodeFramesSuccess(const image::DecodeFramesResult& aResult);
  void OnDecodeFramesFailed(const nsresult& aErr);

  nsCOMPtr<nsIGlobalObject> mParent;
  RefPtr<ImageTrackList> mTracks;
  RefPtr<Promise> mCompletePromise;
  RefPtr<image::SourceBuffer> mSourceBuffer;
  RefPtr<image::AnonymousDecoder> mDecoder;
  AutoTArray<OutstandingDecode, 1> mOutstandingDecodes;
  AutoTArray<RefPtr<VideoFrame>, 1> mDecodedFrames;
  nsAutoString mType;
  bool mComplete = false;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_ImageDecoder_h
