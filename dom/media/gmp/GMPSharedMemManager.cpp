/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPSharedMemManager.h"
#include "mozilla/ipc/SharedMemory.h"

namespace mozilla::gmp {

void GMPSharedMemManager::PurgeSmallerShmem(size_t aSize) {
  mPool.RemoveElementsBy([&](ipc::Shmem& shmem) {
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

bool GMPSharedMemManager::MgrTakeShmem(GMPSharedMemClass aClass, size_t aSize,
                                       ipc::Shmem* aMem) {
  MOZ_ASSERT(MgrIsOnOwningThread());

  // We can only provide shmems for the class that we collect in our pool.
  if (aClass != mCollectClass) {
    return false;
  }

  size_t alignedSize = ipc::SharedMemory::PageAlignedSize(aSize);
  PurgeSmallerShmem(alignedSize);
  if (mPool.IsEmpty()) {
    return MgrAllocShmem(alignedSize, aMem);
  }

  *aMem = mPool.PopLastElement();
  return true;
}

void GMPSharedMemManager::MgrGiveShmem(GMPSharedMemClass aClass,
                                       ipc::Shmem&& aMem) {
  MOZ_ASSERT(MgrIsOnOwningThread());

  if (!aMem.IsWritable()) {
    return;
  }

  // If we are not collecting shmems of this class, return it immediately to the
  // actor on the other side.
  if (aClass != mCollectClass) {
    MgrReturnShmem(aClass, std::move(aMem));
    return;
  }

  PurgeSmallerShmem(mPool, aMem.Size<uint8_t>());

  if (mPool.Length() >= kMaxPoolLength) {
    MgrDeallocShmem(aMem);
    return;
  }

  mPool.AppendElement(std::move(aMem));
}

void GMPSharedMemManager::MgrCreateReturnShmems(GMPSharedMemClass aClass,
                                                size_t aSize) {
  if (aClass == mCollectClass) {
    MOZ_ASSERT_UNREACHABLE("Used wrong shared mem class?");
    return;
  }

  // If the new size is bigger, we know that by sending larger shmems, the
  // remote side will just free the old shmems, so we can simply reset the
  // outstanding count.
  size_t alignedSize = ipc::SharedMemory::PageAlignedSize(aSize);
  if (mReturnShmemSize < alignedSize) {
    mReturnShmemSize = alignedSize;
    mReturnPoolSize = 0;
  }

  if (mReturnShmemSize == 0) {
    return;
  }

  // Due to OOMs, we might temporarily fail to allocate more buffers for the
  // pool, so this is safe to call with the same size.
  while (GMPSharedMemManager::kMaxPoolLength > mReturnPoolSize) {
    ipc::Shmem shmem;
    if (!MgrAllocShmem(mReturnShmemSize, &shmem) ||
        !MgrReturnShmem(aClass, std::move(shmem))) {
      break;
    }
    ++mReturnPoolSize;
  }
}

void GMPSharedMemManager::MgrPurgeShmems() {
  MOZ_ASSERT(MgrIsOnOwningThread());

  for (auto& shmem : mPool) {
    if (shmem.IsWritable()) {
      MgrDeallocShmem(shmem);
    } else {
      MOZ_ASSERT_UNREACHABLE("Shmem must be writable!");
    }
  }
  mPool.Clear();
}

}  // namespace mozilla::gmp
