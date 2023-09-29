/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_TextureRecorded_h
#define mozilla_layers_TextureRecorded_h

#include "TextureClient.h"
#include "mozilla/layers/LayersTypes.h"

namespace mozilla::layers {
class CanvasChild;
class CanvasDrawEventRecorder;

class RecordedTextureData final : public TextureData {
 public:
  RecordedTextureData(gfx::IntSize aSize, gfx::SurfaceFormat aFormat);

  bool Init(TextureType aTextureType);

  void FillInfo(TextureData::Info& aInfo) const final;

  bool Lock(OpenMode aMode) final;

  void Unlock() final;

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() final;

  void EndDraw() final;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot() final;

  void Forget(LayersIPCChannel* aAllocator) final {
    Deallocate(aAllocator);
  }

  void Deallocate(LayersIPCChannel* aAllocator) final;

  bool Serialize(SurfaceDescriptor& aDescriptor) final;

  void OnForwardedToHost() final;

  TextureFlags GetTextureFlags() const final;

 private:
  friend class TextureData;

  DISALLOW_COPY_AND_ASSIGN(RecordedTextureData);

  void DestroyOnOwningThread();

  ~RecordedTextureData() override;

  int64_t mTextureId;

  RefPtr<CanvasChild> mCanvasChild;
  RefPtr<CanvasDrawEventRecorder> mRecorder;
  RefPtr<gfx::DrawTarget> mDT;
  RefPtr<gfx::SourceSurface> mSnapshot;

  gfx::IntSize mSize;
  gfx::SurfaceFormat mFormat;
  OpenMode mLockedMode;
};

}  // namespace mozilla::layers

#endif  // mozilla_layers_TextureRecorded_h
