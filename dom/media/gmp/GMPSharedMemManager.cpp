/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPSharedMemManager.h"

namespace mozilla::gmp {

GMPSharedMemManager::~GMPSharedMemManager() {
  for (size_t i = 0; i < kMaxPools; ++i) {
    MOZ_ASSERT(mPool[i].IsEmpty());
  }
}

void GMPSharedMemManager::PurgeSmallerShmem(nsTArray<ipc::Shmem>& aPool,
                                            size_t aSize) {
  aPool.RemoveElementsBy([&](ipc::Shmem& shmem) {
    if (!shmem.IsWritable()) {
      MOZ_ASSERT_UNREACHABLE("Shmem must be writable!");
      return true;
    }
    if (shmem.Size<uint8_t>() >= aSize) {
      return false;
    }
    MgrDeallocShmem(shmem);
    return true;
  });
}

bool GMPSharedMemManager::MgrTakeShmem(GMPSharedMemClass aClass,
                                       ipc::Shmem* aMem) {
  AssertInOwningThread();

  auto& pool = mPool[size_t(aClass)];
  if (pool.IsEmpty()) {
    return false;
  }

  *aMem = pool.PopLastElement();
  return true;
}

bool GMPSharedMemManager::MgrTakeShmem(GMPSharedMemClass aClass, size_t aSize,
                                       ipc::Shmem* aMem) {
  AssertInOwningThread();

  auto& pool = mPool[size_t(aClass)];
  PurgeSmallerShmem(pool, aSize);
  if (pool.IsEmpty()) {
    return MgrAllocShmem(aSize, aMem);
  }

  *aMem = pool.PopLastElement();
  return true;
}

void GMPSharedMemManager::MgrGiveShmem(GMPSharedMemClass aClass,
                                       ipc::Shmem&& aMem) {
  AssertInOwningThread();

  if (!aMem.IsWritable()) {
    return;
  }

  auto& pool = mPool[size_t(aClass)];
  PurgeSmallerShmem(pool, aMem.Size<uint8_t>());

  if (pool.Length() >= kMaxPoolLength) {
    MgrDeallocShmem(aMem);
    return;
  }

  pool.AppendElement(std::move(aMem));
}

void GMPSharedMemManager::MgrPurgeShmems() {
  AssertInOwningThread();

  for (size_t i = 0; i < kMaxPools; ++i) {
    for (ipc::Shmem& shmem : mPool[i]) {
      if (shmem.IsWritable()) {
        MgrDeallocShmem(shmem);
      } else {
        MOZ_ASSERT_UNREACHABLE("Shmem must be writable!");
      }
    }
    mPool[i].Clear();
  }
}

}  // namespace mozilla::gmp
