/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPShmemManagerParent.h"
#include "mozilla/StaticPrefs_media.h"

namespace mozilla::gmp {

GMPShmemManagerParent::GMPShmemManagerParent()
    : mShmemLimit(StaticPrefs::media_eme_chromium_api_video_shmems()) {}

bool GMPShmemManagerParent::MgrPurgeShmems() {
  GMP_LOG_DEBUG(
      "GMPShmemManagerParent::MgrPurgeShmems(this=%p) frame_size=%zu "
      "limit=%" PRIu32 " active=%" PRIu32,
      this, mBufferSize, mShmemLimit, mShmemsActive);

  if (mShmemsActive == 0) {
    // We haven't allocated any shmems, nothing to do here.
    return true;
  }
  if (!MgrPurgedShmems()) {
    return false;
  }
  mShmemsActive = 0;
  mBufferSize = 0;
  return true;
}

bool GMPShmemManagerParent::MgrEnsureSufficientShmems(size_t aCapacity) {
  GMP_LOG_DEBUG(
      "GMPShmemManagerParent::MgrEnsureSufficientShmems(this=%p) size=%zu "
      "expected_size=%zu limit=%" PRIu32 " active=%" PRIu32,
      this, aCapacity, mBufferSize, mShmemLimit, mShmemsActive);

  if (mBufferSize < aCapacity) {
    if (!MgrPurgeShmems()) {
      return false;
    }
    mBufferSize = aCapacity;
  }

  if (NS_WARN_IF(!mBufferSize)) {
    return false;
  }

  while (mShmemsActive < mShmemLimit) {
    ipc::Shmem shmem;
    if (!AllocShmem(mBufferSize, &shmem)) {
      return false;
    }
    if (!MgrGiveShmem(std::move(shmem))) {
      DeallocShmem(shmem);
      return false;
    }
    mShmemsActive++;
  }

  return true;
}

bool GMPShmemManagerParent::MgrIncreaseShmemPoolSize() {
  GMP_LOG_DEBUG("%s(this=%p) limit=%" PRIu32 " active=%" PRIu32, __func__, this,
                mShmemLimit, mShmemsActive);

  // Put an upper limit on the number of shmems we tolerate the plugin asking
  // for, to prevent a memory blow-out. In practice, we expect the plugin to
  // need less than 5, but some encodings require more.
  if (mShmemLimit > 50) {
    return false;
  }
  mShmemLimit++;

  MgrEnsureSufficientShmems(mBufferSize);
  return true;
}

}  // namespace mozilla::gmp
