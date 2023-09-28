/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_TextureRecorded_h
#define mozilla_layers_TextureRecorded_h

#include "TextureClient.h"
#include "mozilla/Mutex.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/LayersTypes.h"

namespace mozilla {
namespace dom {
class ThreadSafeWorkerRef;
}

namespace layers {

class RecordedTextureData final : public TextureData {
 public:
  RecordedTextureData(gfx::IntSize aSize, gfx::SurfaceFormat aFormat);

  bool Init(TextureType aTextureType);
  void DestroyOnOwningThread();

  void FillInfo(TextureData::Info& aInfo) const final;

  bool Lock(OpenMode aMode) final;

  void Unlock() final;

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() final;

  void EndDraw() final;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot() final;

  void Forget(LayersIPCChannel* aAllocator) final;

  void Deallocate(LayersIPCChannel* aAllocator) final;

  bool Serialize(SurfaceDescriptor& aDescriptor) final;

  void OnForwardedToHost() final;

  TextureFlags GetTextureFlags() const final;

 private:
  friend class TextureData;

  DISALLOW_COPY_AND_ASSIGN(RecordedTextureData);

  ~RecordedTextureData() override;

  int64_t mTextureId;

  Mutex mMutex;
  RefPtr<dom::ThreadSafeWorkerRef> mWorkerRef MOZ_GUARDED_BY(mMutex);
  RefPtr<CanvasChild> mCanvasChild MOZ_GUARDED_BY(mMutex);
  RefPtr<gfx::DrawTarget> mDT MOZ_GUARDED_BY(mMutex);

  gfx::IntSize mSize;
  gfx::SurfaceFormat mFormat;
  RefPtr<gfx::SourceSurface> mSnapshot;
  OpenMode mLockedMode;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_TextureRecorded_h
