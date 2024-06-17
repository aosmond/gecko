/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/VideoFrameProvider.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

VideoFrameRequest::VideoFrameRequest(VideoFrameRequestCallback& aCallback,
                                     uint32_t aHandle)
    : mCallback(&aCallback), mHandle(aHandle) {
  LogVideoFrameRequestCallback::LogDispatch(mCallback);
}

VideoFrameRequest::~VideoFrameRequest() = default;

nsresult VideoFrameRequestManager::Schedule(
    VideoFrameRequestCallback& aCallback, uint32_t* aHandle) {
  if (mCallbackCounter == UINT32_MAX) {
    // Can't increment without overflowing; bail out
    return NS_ERROR_NOT_AVAILABLE;
  }
  uint32_t newHandle = ++mCallbackCounter;

  mCallbacks.AppendElement(VideoFrameRequest(aCallback, newHandle));

  *aHandle = newHandle;
  return NS_OK;
}

bool VideoFrameRequestManager::Cancel(uint32_t aHandle) {
  // mCallbacks is stored sorted by handle
  if (mCallbacks.RemoveElementSorted(aHandle)) {
    return true;
  }

  Unused << mCanceledCallbacks.put(aHandle);
  return false;
}

void VideoFrameRequestManager::Unlink() { mCallbacks.Clear(); }

void VideoFrameRequestManager::Traverse(
    nsCycleCollectionTraversalCallback& aCB) {
  for (auto& i : mCallbacks) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
        aCB, "VideoFrameRequestManager::mCallbacks[i]");
    aCB.NoteXPCOMChild(i.mCallback);
  }
}
}  // namespace mozilla::dom
