/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_VideoFrameCallbackMetadata_h_
#define mozilla_dom_VideoFrameCallbackMetadata_h_

#include "mozilla/dom/HTMLMediaElement.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMNavigationTiming.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class VideoFrameRequestCallback;

class VideoFrameCallbackMetadata final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(VideoFrameCallbackMetadata)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(VideoFrameCallbackMetadata)

  VideoFrameCallbackMetadata(HTMLMediaElement* aElement,
                       DOMHighResTimeStamp aPresentationTime,
                       DOMHighResTimeStamp aExpectedDisplayTime,

                       uint32_t aWidth,
                       uint32_t aHeight,
                       double aMediaTime,

                       uint32_t aPresentedFrames,
                       double aProcessingDuration,

                       DOMHighResTimeStamp aCaptureTime,
                       DOMHighResTimeStamp aReceiveTime,
                       uint32_t aRtpTimestamp
                       );


  HTMLMediaElement* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  DOMHighResTimeStamp PresentationTime() const { return mPresentationTime; }
  DOMHighResTimeStamp ExpectedDisplayTime() const { return mExpectedDisplayTime; }

  uint32_t Width() const { return mWidth; }
  uint32_t Height() const { return mHeight; }
  double MediaTime() const { return mMediaTime; }

  uint32_t PresentedFrames() const { return mPresentedFrames; }
  double ProcessingDuration() const { return mProcessingDuration; }

  DOMHighResTimeStamp CaptureTime() const { return mCaptureTime; }
  DOMHighResTimeStamp ReceiveTime() const { return mReceiveTime; }
  uint32_t RtpTimestamp() const { return mRtpTimestamp; }

 private:
  ~VideoFrameCallbackMetadata() = default;

  RefPtr<HTMLMediaElement> mElement;

  DOMHighResTimeStamp mPresentationTime;
  DOMHighResTimeStamp mExpectedDisplayTime;

  uint32_t mWidth;
  uint32_t mHeight;
  double mMediaTime;

  uint32_t mPresentedFrames;
  double mProcessingDuration;

  DOMHighResTimeStamp mCaptureTime;
  DOMHighResTimeStamp mReceiveTime;
  uint32_t mRtpTimestamp;

};

}  // namespace mozilla::dom

#endif  // mozilla_dom_VideoFrameCallbackMetadata_h_
