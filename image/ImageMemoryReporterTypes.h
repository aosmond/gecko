/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_ImageMemoryReporterTypes_h
#define mozilla_image_ImageMemoryReporterTypes_h

#include <cstdint>
#include "nsString.h"
#include "nsTArray.h"
#include "SurfaceCache.h" // for SurfaceKey

namespace mozilla {
namespace image {

class Image;

struct MemoryCounter
{
  MemoryCounter()
    : mSource(0)
    , mDecodedHeap(0)
    , mDecodedNonHeap(0)
    , mExternalHandles(0)
    , mFrameIndex(0)
    , mExternalId(0)
  { }

  void SetSource(size_t aCount) { mSource = aCount; }
  size_t Source() const { return mSource; }
  void SetDecodedHeap(size_t aCount) { mDecodedHeap = aCount; }
  size_t DecodedHeap() const { return mDecodedHeap; }
  void SetDecodedNonHeap(size_t aCount) { mDecodedNonHeap = aCount; }
  size_t DecodedNonHeap() const { return mDecodedNonHeap; }
  void SetExternalHandles(size_t aCount) { mExternalHandles = aCount; }
  size_t ExternalHandles() const { return mExternalHandles; }
  void SetFrameIndex(size_t aIndex) { mFrameIndex = aIndex; }
  size_t FrameIndex() const { return mFrameIndex; }
  void SetExternalId(uint64_t aId) { mExternalId = aId; }
  uint64_t ExternalId() const { return mExternalId; }

  MemoryCounter& operator+=(const MemoryCounter& aOther)
  {
    mSource += aOther.mSource;
    mDecodedHeap += aOther.mDecodedHeap;
    mDecodedNonHeap += aOther.mDecodedNonHeap;
    mExternalHandles += aOther.mExternalHandles;
    return *this;
  }

private:
  size_t mSource;
  size_t mDecodedHeap;
  size_t mDecodedNonHeap;
  size_t mExternalHandles;
  size_t mFrameIndex;
  uint64_t mExternalId;
};

enum class SurfaceMemoryCounterType
{
  NORMAL,
  COMPOSITING,
  COMPOSITING_PREV,
  ANIMATED_RETAIN,
  ANIMATED_DISCARD,
  ANIMATED_RECYCLE,
};

struct SurfaceMemoryCounter
{
  SurfaceMemoryCounter(const SurfaceKey& aKey,
                       bool aIsLocked,
                       bool aCannotSubstitute,
                       bool aIsFactor2,
                       SurfaceMemoryCounterType aType =
                         SurfaceMemoryCounterType::NORMAL)
    : mKey(aKey)
    , mType(aType)
    , mIsLocked(aIsLocked)
    , mCannotSubstitute(aCannotSubstitute)
    , mIsFactor2(aIsFactor2)
  { }

  const SurfaceKey& Key() const { return mKey; }
  MemoryCounter& Values() { return mValues; }
  const MemoryCounter& Values() const { return mValues; }
  SurfaceMemoryCounterType Type() const { return mType; }
  bool IsLocked() const { return mIsLocked; }
  bool CannotSubstitute() const { return mCannotSubstitute; }
  bool IsFactor2() const { return mIsFactor2; }

private:
  const SurfaceKey mKey;
  MemoryCounter mValues;
  const SurfaceMemoryCounterType mType;
  const bool mIsLocked;
  const bool mCannotSubstitute;
  const bool mIsFactor2;
};

struct ImageMemoryCounter
{
  ImageMemoryCounter(Image* aImage, SizeOfState& aState, bool aIsUsed);

  nsCString& URI() { return mURI; }
  const nsCString& URI() const { return mURI; }
  const nsTArray<SurfaceMemoryCounter>& Surfaces() const { return mSurfaces; }
  const gfx::IntSize IntrinsicSize() const { return mIntrinsicSize; }
  const MemoryCounter& Values() const { return mValues; }
  uint16_t Type() const { return mType; }
  bool IsUsed() const { return mIsUsed; }

  bool IsNotable() const
  {
    const size_t NotableThreshold = 16 * 1024;
    size_t total = mValues.Source() + mValues.DecodedHeap()
                                    + mValues.DecodedNonHeap();
    return total >= NotableThreshold;
  }

private:
  nsCString mURI;
  nsTArray<SurfaceMemoryCounter> mSurfaces;
  gfx::IntSize mIntrinsicSize;
  MemoryCounter mValues;
  uint16_t mType;
  const bool mIsUsed;
};

} // image
} // mozilla

#endif // mozilla_image_ImageMemoryReporterTypes_h
