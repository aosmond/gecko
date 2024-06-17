#include "mozilla/dom/VideoFrameProvider.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

VideoFrameRequest::VideoFrameRequest(VideoFrameRequestCallback& aCallback, int32_t aHandle)
    : mCallback(&aCallback), mHandle(aHandle) {
  //LogVideoFrameRequestCallback::LogDispatch(mCallback);
}

VideoFrameRequest::~VideoFrameRequest() = default;

nsresult VideoFrameRequestManager::Schedule(VideoFrameRequestCallback& aCallback,
                                       int32_t* aHandle) {
  if (mCallbackCounter == INT32_MAX) {
    // Can't increment without overflowing; bail out
    return NS_ERROR_NOT_AVAILABLE;
  }
  int32_t newHandle = ++mCallbackCounter;

  mCallbacks.AppendElement(VideoFrameRequest(aCallback, newHandle));

  *aHandle = newHandle;
  return NS_OK;
}

bool VideoFrameRequestManager::Cancel(int32_t aHandle) {
  // mCallbacks is stored sorted by handle
  if (mCallbacks.RemoveElementSorted(aHandle)) {
    return true;
  }

  Unused << mCanceledCallbacks.put(aHandle);
  return false;
}

void VideoFrameRequestManager::Unlink() { mCallbacks.Clear(); }

void VideoFrameRequestManager::Traverse(nsCycleCollectionTraversalCallback& aCB) {
  for (auto& i : mCallbacks) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCB,
                                       "VideoFrameRequestManager::mCallbacks[i]");
    aCB.NoteXPCOMChild(i.mCallback);
  }
}
}
