/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPSharedMemManager_h_
#define GMPSharedMemManager_h_

#include "mozilla/ipc/Shmem.h"
#include "nsTArray.h"

namespace mozilla::gmp {

enum class GMPSharedMemClass { Decoded, Encoded };

class GMPSharedMemManager {
 public:
  static constexpr size_t kMaxPoolLength = 16;

  explicit GMPSharedMemManager(GMPSharedMemClass aCollectClass)
      : mCollectClass(aCollectClass) {}

  virtual ~GMPSharedMemManager() { MOZ_ASSERT(mPool.IsEmpty()); }

  bool MgrTakeShmem(GMPSharedMemClass aClass, size_t aSize, ipc::Shmem* aMem);
  void MgrGiveShmem(GMPSharedMemClass aClass, ipc::Shmem&& aMem);
  void MgrPurgeShmems();

  void MgrCreateReturnShmems(GMPSharedMemClass aClass, size_t aSize);

  virtual bool MgrAllocShmem(size_t aSize, ipc::Shmem* aMem) { return false; }
  virtual void MgrDeallocShmem(ipc::Shmem& aMem) = 0;

 protected:
  virtual bool MgrReturnShmem(GMPSharedMemClass aClass, ipc::Shmem&& aMem) = 0;
  virtual bool MgrIsOnOwningThread() const = 0;

 private:
  void PurgeSmallerShmem(nsTArray<ipc::Shmem>& aPool, size_t aSize);

  AutoTArray<ipc::Shmem, kMaxPoolLength> mPool;
  size_t mReturnShmemSize = 0;
  size_t mReturnPoolSize = 0;
  GMPSharedMemClass mCollectClass;
};

}  // namespace mozilla::gmp

#endif  // GMPSharedMemManager_h_
