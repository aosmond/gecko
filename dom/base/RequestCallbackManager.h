/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RequestCallbackManager_h
#define mozilla_dom_RequestCallbackManager_h

#include <limits>
#include "mozilla/HashTable.h"
#include "mozilla/RefPtr.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

template <typename RequestCallback, typename RequestCallbackHandle>
struct RequestCallbackEntry {
  RequestCallbackEntry(RequestCallback& aCallback,
                       RequestCallbackHandle aHandle)
      : mCallback(&aCallback), mHandle(aHandle) {
    LogTaskBase<RequestCallback>::LogDispatch(mCallback);
  }

  ~RequestCallbackEntry() = default;

  // Comparator operators to allow RemoveElementSorted with an
  // integer argument on arrays of RequestCallback
  bool operator==(RequestCallbackHandle aHandle) const {
    return mHandle == aHandle;
  }
  bool operator<(RequestCallbackHandle aHandle) const {
    return mHandle < aHandle;
  }

  RefPtr<RequestCallback> mCallback;
  RequestCallbackHandle mHandle;
};

template <typename RequestCallback, typename RequestCallbackHandle>
class RequestCallbackManager {
 public:
  RequestCallbackManager() = default;
  ~RequestCallbackManager() = default;

  nsresult Schedule(RequestCallback& aCallback,
                    RequestCallbackHandle* aHandle) {
    if (mCallbackCounter == std::numeric_limits<RequestCallbackHandle>::max()) {
      // Can't increment without overflowing; bail out
      return NS_ERROR_NOT_AVAILABLE;
    }
    RequestCallbackHandle newHandle = ++mCallbackCounter;

    mCallbacks.AppendElement(RequestCallbackEntry(aCallback, newHandle));

    *aHandle = newHandle;
    return NS_OK;
  }

  bool Cancel(RequestCallbackHandle aHandle) {
    // mCallbacks is stored sorted by handle
    if (mCallbacks.RemoveElementSorted(aHandle)) {
      return true;
    }

    Unused << mCanceledCallbacks.put(aHandle);
    return false;
  }

  bool IsEmpty() const { return mCallbacks.IsEmpty(); }

  bool IsCanceled(RequestCallbackHandle aHandle) const {
    return !mCanceledCallbacks.empty() && mCanceledCallbacks.has(aHandle);
  }

  void Take(
      nsTArray<RequestCallbackEntry<RequestCallback, RequestCallbackHandle>>&
          aCallbacks) {
    aCallbacks = std::move(mCallbacks);
    mCanceledCallbacks.clear();
  }

  void Unlink() { mCallbacks.Clear(); }

  void Traverse(nsCycleCollectionTraversalCallback& aCB) {
    for (auto& i : mCallbacks) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
          aCB, "RequestCallbackManager::mCallbacks[i]");
      aCB.NoteXPCOMChild(i.mCallback);
    }
  }

 private:
  nsTArray<RequestCallbackEntry<RequestCallback, RequestCallbackHandle>>
      mCallbacks;

  // The set of frame request callbacks that were canceled but which we failed
  // to find in mRequestCallbacks.
  HashSet<RequestCallbackHandle> mCanceledCallbacks;

  /**
   * The current frame request callback handle
   */
  RequestCallbackHandle mCallbackCounter = 0;
};

template <class RequestCallback, typename RequestCallbackHandle>
inline void ImplCycleCollectionUnlink(
    RequestCallbackManager<RequestCallback, RequestCallbackHandle>& aField) {
  aField.Unlink();
}

template <class RequestCallback, typename RequestCallbackHandle>
inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    RequestCallbackManager<RequestCallback, RequestCallbackHandle>& aField,
    const char* aName, uint32_t aFlags) {
  aField.Traverse(aCallback);
}

}  // namespace mozilla::dom

#endif
