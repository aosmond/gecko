/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPShmemManagerChild.h"
#include <limits>

namespace mozilla::gmp {

static auto ToString(const nsTArray<ipc::Shmem>& aBuffers) {
  return StringJoin(","_ns, aBuffers, [](auto& s, const ipc::Shmem& shmem) {
    s.AppendInt(static_cast<uint32_t>(shmem.Size<uint8_t>()));
  });
}

GMPShmemManagerChild::~GMPShmemManagerChild() { PurgeShmems(); }

bool GMPShmemManagerChild::TakeShmem(size_t aCapacity, ipc::Shmem* aShmem) {
  GMP_LOG_DEBUG(
      "GMPShmemManagerChild::TakeShmem(this=%p capacity=%zu) bufferSizes={%s}",
      this, aCapacity, ToString(mBuffers).get());

  if (mBuffers.IsEmpty()) {
    ProtoRequestShmems();
  }

  // Find the shmem with the least amount of wasted space if we were to
  // select it for this sized allocation.
  const size_t invalid = std::numeric_limits<size_t>::max();
  size_t best = invalid;
  auto wastedSpace = [this, aCapacity](size_t index) {
    return mBuffers[index].Size<uint8_t>() - aCapacity;
  };
  for (size_t i = 0; i < mBuffers.Length(); i++) {
    if (mBuffers[i].Size<uint8_t>() >= aCapacity &&
        (best == invalid || wastedSpace(i) < wastedSpace(best))) {
      best = i;
    }
  }
  if (best == invalid) {
    // The parent process should have bestowed upon us a shmem of appropriate
    // size, but did not!
    return false;
  }
  *aShmem = std::move(mBuffers[best]);
  mBuffers.RemoveElementAt(best);
  return true;
}

bool GMPShmemManagerChild::HasShmem(size_t aCapacity) {
  for (const ipc::Shmem& shmem : mBuffers) {
    if (shmem.Size<uint8_t>() == aCapacity) {
      return true;
    }
  }
  return false;
}

void GMPShmemManagerChild::ForgetShmem(size_t aCapacity) {
  mBuffers.RemoveElementsBy([&](ipc::Shmem& aShmem) {
    if (aShmem.Size<uint8_t>() != aCapacity) {
      return false;
    }
    ProtoDeallocShmem(aShmem);
    return true;
  });
}

void GMPShmemManagerChild::GiveShmem(ipc::Shmem&& aShmem) {
  size_t sz = aShmem.Size<uint8_t>();
  mBuffers.AppendElement(std::move(aShmem));
  GMP_LOG_DEBUG(
      "GMPShmemManagerChild::RecvGiveBuffer(this=%p, capacity=%zu) "
      "bufferSizes={%s}",
      this, sz, ToString(mBuffers).get());
}

void GMPShmemManagerChild::PurgeShmems() {
  for (ipc::Shmem& shmem : mBuffers) {
    ProtoDeallocShmem(shmem);
  }
  mBuffers.Clear();
}

}  // namespace mozilla::gmp
