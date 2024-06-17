/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoFrameCallbackMetadata.h"

#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/VideoFrameCallbackMetadataBinding.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

VideoFrameCallbackMetadata::VideoFrameCallbackMetadata(HTMLMediaElement* aElement,
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
                       )
    : mElement(aElement),
      mPresentationTime(aPresentationTime),
      mExpectedDisplayTime(aExpectedDisplayTime),
      mWidth(aWidth),
      mHeight(aHeight),
      mMediaTime(aMediaTime),
      mPresentedFrames(aPresentedFrames),
      mProcessingDuration(aProcessingDuration),
      mCaptureTime(aCaptureTime),
      mReceiveTime(aReceiveTime),
      mRtpTimestamp(aRtpTimestamp)
    {}

HTMLMediaElement* VideoFrameCallbackMetadata::GetParentObject() const {
  return mElement;
}

JSObject* VideoFrameCallbackMetadata::WrapObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return VideoFrameCallbackMetadata_Binding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(VideoFrameCallbackMetadata, mElement)

}  // namespace mozilla::dom
