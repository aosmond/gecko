/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPShmemManagerChild_h_
#define GMPShmemManagerChild_h_

#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/Unused.h"
#include "nsTArray.h"

#define GMP_INLINE_DECL_SHMEM_MANAGER_CHILD(protocol)                          \
  FORWARD_SHMEM_ALLOCATOR_TO(protocol)                                         \
  mozilla::ipc::IPCResult RecvGiveBuffer(mozilla::ipc::Shmem&& aShmem) final { \
    MgrGiveShmem(std::move(aShmem));                                           \
    return IPC_OK();                                                           \
  }                                                                            \
  mozilla::ipc::IPCResult RecvPurgeShmems() final {                            \
    MgrPurgeShmems();                                                          \
    return IPC_OK();                                                           \
  }                                                                            \
  void MgrRequestShmems() final {                                              \
    mozilla::Unused << SendIncreaseShmemPoolSize();                            \
  }

namespace mozilla::gmp {

class GMPShmemManagerChild : public ipc::IShmemAllocator {
 public:
  GMPShmemManagerChild() = default;

  bool MgrTakeShmem(size_t aCapacity, ipc::Shmem* aShmem);
  bool MgrHasShmem(size_t aCapacity);
  void MgrForgetShmem(size_t aCapacity);
  void MgrGiveShmem(ipc::Shmem&& aShmem);
  void MgrPurgeShmems();

 protected:
  virtual void MgrRequestShmems() = 0;
  virtual ~GMPShmemManagerChild();

 private:
  nsTArray<ipc::Shmem> mBuffers;
};

}  // namespace mozilla::gmp

#endif
