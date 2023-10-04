/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SourceSurfaceCanvasRecording_h
#define mozilla_layers_SourceSurfaceCanvasRecording_h

#include "mozilla/gfx/2D.h"

namespace mozilla {
namespace dom {
class ThreadSafeWorkerRef;
}

namespace layers {
class CanvasChild;
class CanvasDrawEventRecorder;

class SourceSurfaceCanvasRecording final : public gfx::SourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceCanvasRecording, final)

  SourceSurfaceCanvasRecording(
      const RefPtr<gfx::SourceSurface>& aRecordedSuface,
      CanvasChild* aCanvasChild,
      const RefPtr<CanvasDrawEventRecorder>& aRecorder);

  ~SourceSurfaceCanvasRecording() final;

  bool Init();

  gfx::SurfaceType GetType() const final { return mRecordedSurface->GetType(); }

  gfx::IntSize GetSize() const final { return mRecordedSurface->GetSize(); }

  gfx::SurfaceFormat GetFormat() const final {
    return mRecordedSurface->GetFormat();
  }

  already_AddRefed<gfx::DataSourceSurface> GetDataSurface() final;

 private:
  void DestroyOnOwningThreadLocked() MOZ_REQUIRES(mMutex);
  void DestroyOnOwningThread();
  bool IsOnOwningThread() const;
  void EnsureDataSurfaceOnOwningThread();

  mutable Mutex mMutex;
  RefPtr<dom::ThreadSafeWorkerRef> mWorkerRef MOZ_GUARDED_BY(mMutex);

  RefPtr<gfx::SourceSurface> mRecordedSurface;
  RefPtr<CanvasChild> mCanvasChild;
  RefPtr<CanvasDrawEventRecorder> mRecorder;
  RefPtr<gfx::DataSourceSurface> mDataSourceSurface;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_SourceSurfaceCanvasRecording_h
