/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageDecoderReadRequest.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/dom/ImageDecoder.h"
#include "mozilla/dom/ReadableStreamDefaultReader.h"
#include "mozilla/image/SourceBuffer.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(ImageDecoderReadRequest, ReadRequest,
                                   mDecoder, mReader)
NS_IMPL_ADDREF_INHERITED(ImageDecoderReadRequest, ReadRequest)
NS_IMPL_RELEASE_INHERITED(ImageDecoderReadRequest, ReadRequest)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageDecoderReadRequest)
NS_INTERFACE_MAP_END_INHERITING(ReadRequest)

ImageDecoderReadRequest::ImageDecoderReadRequest(
    image::SourceBuffer* aSourceBuffer)
    : mSourceBuffer(std::move(aSourceBuffer)) {}

ImageDecoderReadRequest::~ImageDecoderReadRequest() = default;

bool ImageDecoderReadRequest::Initialize(const GlobalObject& aGlobal,
                                         ImageDecoder* aDecoder,
                                         ReadableStream& aStream) {
  IgnoredErrorResult rv;
  mReader = ReadableStreamDefaultReader::Constructor(aGlobal, aStream, rv);
  if (NS_WARN_IF(rv.Failed())) {
    mSourceBuffer->Complete(NS_ERROR_FAILURE);
    return false;
  }

  mDecoder = aDecoder;
  QueueRead();
  return true;
}

void ImageDecoderReadRequest::Destroy() {
  mDecoder = nullptr;
  Complete(NS_ERROR_ABORT);
}

void ImageDecoderReadRequest::QueueRead() {
  class ReadMicroTask final : public MicroTaskRunnable {
   public:
    explicit ReadMicroTask(ImageDecoderReadRequest* aOwner) : mOwner(aOwner) {}

    MOZ_CAN_RUN_SCRIPT void Run(AutoSlowOperation& aAso) override {
      mOwner->Read();
    }

   private:
    MOZ_KNOWN_LIVE RefPtr<ImageDecoderReadRequest> mOwner;
  };

  CycleCollectedJSContext* context = CycleCollectedJSContext::Get();
  if (NS_WARN_IF(!context)) {
    Complete(NS_ERROR_FAILURE);
    return;
  }

  printf_stderr("[AO] [%p] %s %d -- queue read\n", this, __func__, __LINE__);
  auto task = MakeRefPtr<ReadMicroTask>(this);
  context->DispatchToMicroTask(task.forget());
}

void ImageDecoderReadRequest::Read() {
  CycleCollectedJSContext* context = CycleCollectedJSContext::Get();
  if (NS_WARN_IF(!context)) {
    Complete(NS_ERROR_FAILURE);
    return;
  }

  printf_stderr("[AO] [%p] %s %d -- read\n", this, __func__, __LINE__);
  RefPtr<ImageDecoderReadRequest> self(this);
  RefPtr<ReadableStreamDefaultReader> reader(mReader);

  IgnoredErrorResult err;
  JSContext* cx = context->Context();
  reader->ReadChunk(cx, *self, err);
  if (NS_WARN_IF(err.Failed())) {
    Complete(NS_ERROR_FAILURE);
  }
}

void ImageDecoderReadRequest::Complete(nsresult aErr) {
  printf_stderr("[AO] [%p] %s %d -- complete %d\n", this, __func__, __LINE__, NS_SUCCEEDED(aErr));

  if (mSourceBuffer && !mSourceBuffer->IsComplete()) {
    mSourceBuffer->Complete(aErr);
  }

  if (mDecoder) {
    mDecoder->OnSourceBufferComplete(aErr);
    mDecoder = nullptr;
  }
}

void ImageDecoderReadRequest::ChunkSteps(JSContext* aCx,
                                         JS::Handle<JS::Value> aChunk,
                                         ErrorResult& aRv) {
  RootedSpiderMonkeyInterface<Uint8Array> chunk(aCx);
  if (!aChunk.isObject() || !chunk.Init(&aChunk.toObject())) {
    Complete(NS_ERROR_FAILURE);
    return;
  }
  chunk.ComputeState();

  printf_stderr("[AO] [%p] %s %d -- append chunk %u\n", this, __func__, __LINE__, chunk.Length());
  nsresult rv = mSourceBuffer->Append(
      reinterpret_cast<const char*>(chunk.Data()), chunk.Length());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Complete(NS_ERROR_FAILURE);
    return;
  }

  QueueRead();
}

void ImageDecoderReadRequest::CloseSteps(JSContext* aCx, ErrorResult& aRv) {
  printf_stderr("[AO] [%p] %s %d -- close\n", this, __func__, __LINE__);
  Complete(NS_OK);
}

void ImageDecoderReadRequest::ErrorSteps(JSContext* aCx,
                                         JS::Handle<JS::Value> aError,
                                         ErrorResult& aRv) {
  printf_stderr("[AO] [%p] %s %d -- error\n", this, __func__, __LINE__);
  Complete(NS_ERROR_FAILURE);
}

}  // namespace mozilla::dom
