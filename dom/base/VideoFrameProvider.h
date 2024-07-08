#ifndef mozilla_dom_VideoFrameProvider_h
#define mozilla_dom_VideoFrameProvider_h

#include "mozilla/HashTable.h"
#include "mozilla/RefPtr.h"
#include "nsTArray.h"

#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/HTMLVideoElementBinding.h"

namespace mozilla::dom {

// class VideoFrameCallbackMetadata;
// class VideoFrameRequestCallback;

// template me OK
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

// NON_VIRTUAL_ADDREF_RELEASE(mozilla::dom::VideoFrameRequest)

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
