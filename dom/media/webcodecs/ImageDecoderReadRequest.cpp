/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageDecoderReadRequest.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/dom/ImageDecoder.h"
#include "mozilla/dom/ReadableStreamDefaultReader.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/image/SourceBuffer.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(ImageDecoderReadRequest, ReadRequest,
                                   mDecoder, mStream, mReader)
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
  if (WorkerPrivate* wp = GetCurrentThreadWorkerPrivate()) {
    mWorkerRef =
        WeakWorkerRef::Create(wp, [self = RefPtr{this}]() { self->Destroy(); });
    if (NS_WARN_IF(!mWorkerRef)) {
      mSourceBuffer->Complete(NS_ERROR_FAILURE);
      Destroy();
      return false;
    }
  }

  IgnoredErrorResult rv;
  mReader = aStream.GetReader(rv);
  if (NS_WARN_IF(rv.Failed())) {
    mSourceBuffer->Complete(NS_ERROR_FAILURE);
    Destroy();
    return false;
  }

  mDecoder = aDecoder;
  mStream = &aStream;
  QueueRead();
  return true;
}

void ImageDecoderReadRequest::Destroy() {
  if (mReader && mStream &&
      mStream->State() == ReadableStream::ReaderState::Readable) {
    AutoJSAPI jsapi;
    MOZ_ALWAYS_TRUE(jsapi.Init(mDecoder->GetParentObject()));

    ErrorResult rv;
    rv.ThrowAbortError("ImageDecoderReadRequest destroyed");

    JS::Rooted<JS::Value> errorValue(jsapi.cx());
    if (ToJSValue(jsapi.cx(), std::move(rv), &errorValue)) {
      IgnoredErrorResult ignoredRv;
      if (RefPtr<Promise> p =
              mReader->Cancel(jsapi.cx(), errorValue, ignoredRv)) {
        MOZ_ALWAYS_TRUE(p->SetAnyPromiseIsHandled());
      }
    }

    jsapi.ClearException();
  }

  if (mSourceBuffer) {
    if (!mSourceBuffer->IsComplete()) {
      mSourceBuffer->Complete(NS_ERROR_ABORT);
    }
    mSourceBuffer = nullptr;
  }

  mDecoder = nullptr;
  mReader = nullptr;
  mStream = nullptr;
}

void ImageDecoderReadRequest::QueueRead() {
  class ReadRunnable final : public CancelableRunnable {
   public:
    explicit ReadRunnable(ImageDecoderReadRequest* aOwner)
        : CancelableRunnable(
              "mozilla::dom::ImageDecoderReadRequest::QueueRead"),
          mOwner(aOwner) {}

    NS_IMETHODIMP Run() override {
      mOwner->Read();
      mOwner = nullptr;
      return NS_OK;
    }

    nsresult Cancel() override {
      mOwner->Complete(NS_ERROR_ABORT);
      mOwner = nullptr;
      return NS_OK;
    }

   private:
    virtual ~ReadRunnable() {
      if (mOwner) {
        Cancel();
      }
    }

    RefPtr<ImageDecoderReadRequest> mOwner;
  };

  if (!mReader) {
    return;
  }

  auto task = MakeRefPtr<ReadRunnable>(this);
  NS_DispatchToCurrentThread(task.forget());
}

void ImageDecoderReadRequest::Read() {
  if (!mReader) {
    return;
  }

  AutoJSAPI jsapi;
  jsapi.Init();

  RefPtr<ImageDecoderReadRequest> self(this);
  RefPtr<ReadableStreamDefaultReader> reader(mReader);

  IgnoredErrorResult err;
  reader->ReadChunk(jsapi.cx(), *self, err);
  if (NS_WARN_IF(err.Failed())) {
    Complete(NS_ERROR_FAILURE);
  }
}

void ImageDecoderReadRequest::Complete(nsresult aErr) {
  if (mSourceBuffer && !mSourceBuffer->IsComplete()) {
    mSourceBuffer->Complete(aErr);
  }

  if (mDecoder) {
    mDecoder->OnSourceBufferComplete(aErr);
    mDecoder = nullptr;
  }

  mReader = nullptr;
  mStream = nullptr;
}

void ImageDecoderReadRequest::ChunkSteps(JSContext* aCx,
                                         JS::Handle<JS::Value> aChunk,
                                         ErrorResult& aRv) {
  RootedSpiderMonkeyInterface<Uint8Array> chunk(aCx);
  if (!aChunk.isObject() || !chunk.Init(&aChunk.toObject())) {
    Complete(NS_ERROR_FAILURE);
    return;
  }
  chunk.ProcessData([&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&&) {
    nsresult rv = mSourceBuffer->Append(
        reinterpret_cast<const char*>(aData.Elements()), aData.Length());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Complete(NS_ERROR_FAILURE);
      return;
    }
  });
}

void ImageDecoderReadRequest::CloseSteps(JSContext* aCx, ErrorResult& aRv) {
  Complete(NS_OK);
}

void ImageDecoderReadRequest::ErrorSteps(JSContext* aCx,
                                         JS::Handle<JS::Value> aError,
                                         ErrorResult& aRv) {
  Complete(NS_ERROR_FAILURE);
}

}  // namespace mozilla::dom
