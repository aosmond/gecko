/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPShmemManagerParent_h_
#define GMPShmemManagerParent_h_

#include "mozilla/ipc/Shmem.h"

#define GMP_INLINE_DECL_SHMEM_MANAGER_PARENT                                  \
  bool ProtoPurgeShmems() final { return SendPurgeShmems(); }                 \
  bool ProtoGiveShmem(mozilla::ipc::Shmem&& aShmem) final {                   \
    return SendGiveBuffer(std::move(aShmem));                                 \
  }                                                                           \
  bool ProtoAllocShmem(size_t aCapacity, mozilla::ipc::Shmem* aShmem) final { \
    return AllocShmem(aCapacity, aShmem);                                     \
  }                                                                           \
  bool ProtoDeallocShmem(mozilla::ipc::Shmem& aShmem) final {                 \
    return DeallocShmem(aShmem);                                              \
  }                                                                           \
  mozilla::ipc::IPCResult RecvIncreaseShmemPoolSize();

namespace mozilla::gmp {

class GMPShmemManagerParent {
 protected:
  GMPShmemManagerParent();
  virtual ~GMPShmemManagerParent() = default;

  bool PurgeShmems();
  bool EnsureSufficientShmems(size_t aCapacity = 0);
  bool IncreaseShmemPoolSize();

  virtual bool ProtoPurgeShmems() = 0;
  virtual bool ProtoGiveShmem(ipc::Shmem&& aShmem) = 0;
  virtual bool ProtoAllocShmem(size_t aCapacity, ipc::Shmem* aShmem) = 0;
  virtual bool ProtoDeallocShmem(ipc::Shmem& aShmem) = 0;

 private:
  size_t mBufferSize = 0;

  // Count of the number of shmems in the set used to return frames from the
  // plugin to Gecko.
  uint32_t mShmemsActive = 0;
  // Maximum number of shmems to use.
  uint32_t mShmemLimit;
};

}  // namespace mozilla::gmp

#endif
