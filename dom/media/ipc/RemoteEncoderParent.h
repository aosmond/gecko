/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteDecoderParent_h
#define include_dom_media_ipc_RemoteDecoderParent_h

#include "mozilla/PRemoteEncoderParent.h"
#include "mozilla/ShmemRecycleAllocator.h"

namespace mozilla {

class RemoteMediaManagerParent;
using mozilla::ipc::IPCResult;

class RemoteEncoderParent : public ShmemRecycleAllocator<RemoteEncoderParent>,
                            public PRemoteEncoderParent {
  friend class PRemoteEncoderParent;

 public:
  // We refcount this class since the task queue can have runnables
  // that reference us.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteEncoderParent)

  RemoteEncoderParent(RemoteMediaManagerParent* aParent,
                      const CreateDecoderParams::OptionSet& aOptions,
                      nsISerialEventTarget* aManagerThread,
                      TaskQueue* aEncodeTaskQueue,
                      const Maybe<uint64_t>& aMediaEngineId,
                      Maybe<TrackingId> aTrackingId);

  void Destroy();

  // PRemoteEncoderParent
  virtual IPCResult RecvConstruct(ConstructResolver&& aResolver) = 0;
  IPCResult RecvInit(InitResolver&& aResolver);
  IPCResult RecvEncode(EncodedInputIPDL* aData,
                       EncodeResolver&& aResolver);
  IPCResult RecvReconfigure(ArrayOfRemoteEncoderConfigurationItem* aConfig, ReconfigureResolver&& aResolver);
  IPCResult RecvDrain(DrainResolver&& aResolver);
  IPCResult RecvShutdown(ShutdownResolver&& aResolver);
  IPCResult RecvSetBitrate(const uint64_t& aBitrate, SetBitrateResolver&& aResolver);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 protected:
  virtual ~RemoteEncoderParent();

  bool OnManagerThread();

  const RefPtr<RemoteMediaManagerParent> mParent;
  const RefPtr<TaskQueue> mEncodeTaskQueue;
  RefPtr<MediaDataEncoder> mEncoder;

 private:
  RefPtr<RemoteEncoderParent> mIPDLSelfRef;
  const RefPtr<nsISerialEventTarget> mManagerThread;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteDecoderParent_h
