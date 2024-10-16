/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPShmemManagerChild_h_
#define GMPShmemManagerChild_h_

#include "mozilla/ipc/Shmem.h"
#include "mozilla/Unused.h"
#include "nsTArray.h"

#define GMP_INLINE_DECL_SHMEM_MANAGER_CHILD                                    \
  mozilla::ipc::IPCResult RecvGiveBuffer(mozilla::ipc::Shmem&& aShmem) final { \
    GiveShmem(std::move(aShmem));                                              \
    return IPC_OK();                                                           \
  }                                                                            \
  mozilla::ipc::IPCResult RecvPurgeShmems() final {                            \
    PurgeShmems();                                                             \
    return IPC_OK();                                                           \
  }                                                                            \
  void ProtoRequestShmems() final {                                            \
    mozilla::Unused << SendIncreaseShmemPoolSize();                            \
  }                                                                            \
  bool ProtoDeallocShmem(mozilla::ipc::Shmem& aShmem) final {                  \
    return DeallocShmem(aShmem);                                               \
  }

namespace mozilla::gmp {

class GMPShmemManagerChild {
 public:
  GMPShmemManagerChild() = default;

  bool TakeShmem(size_t aCapacity, ipc::Shmem* aShmem);
  bool HasShmem(size_t aCapacity);
  void ForgetShmem(size_t aCapacity);
  void GiveShmem(ipc::Shmem&& aShmem);
  void PurgeShmems();

 protected:
  virtual void ProtoRequestShmems() = 0;
  virtual bool ProtoDeallocShmem(ipc::Shmem& aShmem) = 0;
  virtual ~GMPShmemManagerChild();

 private:
  nsTArray<ipc::Shmem> mBuffers;
};

}  // namespace mozilla::gmp

#endif
