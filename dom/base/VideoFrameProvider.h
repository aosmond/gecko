/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_VideoFrameProvider_h
#define mozilla_dom_VideoFrameProvider_h

#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/HTMLVideoElementBinding.h"
#include "mozilla/HashTable.h"
#include "mozilla/RefPtr.h"
#include "nsTArray.h"

namespace mozilla::dom {

struct VideoFrameRequest {
  VideoFrameRequest(VideoFrameRequestCallback& aCallback, uint32_t aHandle);
  ~VideoFrameRequest();

  // Comparator operators to allow RemoveElementSorted with an
  // integer argument on arrays of VideoFrameRequest
  bool operator==(uint32_t aHandle) const { return mHandle == aHandle; }
  bool operator<(uint32_t aHandle) const { return mHandle < aHandle; }

  RefPtr<VideoFrameRequestCallback> mCallback;
  uint32_t mHandle;
};

class VideoFrameRequestManager final {
 public:
  VideoFrameRequestManager() = default;
  ~VideoFrameRequestManager() = default;

  nsresult Schedule(VideoFrameRequestCallback& aCallback, uint32_t* aHandle);
  bool Cancel(uint32_t aHandle);

  bool IsEmpty() const { return mCallbacks.IsEmpty(); }

  bool IsCanceled(uint32_t aHandle) const {
    return !mCanceledCallbacks.empty() && mCanceledCallbacks.has(aHandle);
  }

  void Take(nsTArray<VideoFrameRequest>& aCallbacks) {
    aCallbacks = std::move(mCallbacks);
    mCanceledCallbacks.clear();
  }

  void Unlink();

  void Traverse(nsCycleCollectionTraversalCallback& aCB);

 private:
  nsTArray<VideoFrameRequest> mCallbacks;

  // The set of frame request callbacks that were canceled but which we failed
  // to find in mFrameRequestCallbacks.
  mozilla::HashSet<uint32_t> mCanceledCallbacks;

  /**
   * The current frame request callback handle
   */
  uint32_t mCallbackCounter = 0;
};

inline void ImplCycleCollectionUnlink(VideoFrameRequestManager& aField) {
  aField.Unlink();
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    VideoFrameRequestManager& aField, const char* aName, uint32_t aFlags) {
  aField.Traverse(aCallback);
}
}  // namespace mozilla::dom
#endif  // mozilla_dom_VideoFrameProvider_h
