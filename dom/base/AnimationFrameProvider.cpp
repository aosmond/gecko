/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/AnimationFrameProvider.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/WorkerCommon.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

FrameRequest::FrameRequest(FrameRequestCallback& aCallback, int32_t aHandle)
    : mCallback(&aCallback), mHandle(aHandle) {
  LogFrameRequestCallback::LogDispatch(mCallback);
}

FrameRequest::~FrameRequest() = default;

FrameRequestManager::FrameRequestManager() = default;

FrameRequestManager::~FrameRequestManager() = default;

nsresult FrameRequestManager::Schedule(FrameRequestCallback& aCallback,
                                       int32_t* aHandle) {
  if (mCallbackCounter == INT32_MAX) {
    // Can't increment without overflowing; bail out
    return NS_ERROR_NOT_AVAILABLE;
  }
  int32_t newHandle = ++mCallbackCounter;

  mCallbacks.AppendElement(FrameRequest(aCallback, newHandle));

  *aHandle = newHandle;
  return NS_OK;
}

void FrameRequestManager::Schedule(HTMLVideoElement* aElement) {
  mVideoCallbacks.AppendElement(aElement);
}

bool FrameRequestManager::Cancel(HTMLVideoElement* aElement) {
  return mVideoCallbacks.RemoveElement(aElement);
}

bool FrameRequestManager::Cancel(int32_t aHandle) {
  // mCallbacks is stored sorted by handle
  if (mCallbacks.RemoveElementSorted(aHandle)) {
    return true;
  }

  Unused << mCanceledCallbacks.put(aHandle);
  return false;
}

void FrameRequestManager::Take(
    nsTArray<RefPtr<HTMLVideoElement>>& aVideoCallbacks) {
  MOZ_ASSERT(NS_IsMainThread());
  aVideoCallbacks = std::move(mVideoCallbacks);
}

void FrameRequestManager::Take(nsTArray<FrameRequest>& aCallbacks) {
  aCallbacks = std::move(mCallbacks);
  mCanceledCallbacks.clear();
}

void FrameRequestManager::Unlink() {
  mCallbacks.Clear();
  mVideoCallbacks.Clear();
}

void FrameRequestManager::Traverse(nsCycleCollectionTraversalCallback& aCB) {
  for (auto& i : mCallbacks) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCB,
                                       "FrameRequestManager::mCallbacks[i]");
    aCB.NoteXPCOMChild(i.mCallback);
  }
  for (auto& i : mVideoCallbacks) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
        aCB, "FrameRequestManager::mVideoCallbacks[i]");
    aCB.NoteXPCOMChild(ToSupports(i));
  }
}

}  // namespace mozilla::dom
