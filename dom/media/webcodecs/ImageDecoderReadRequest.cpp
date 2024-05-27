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

NS_IMPL_CYCLE_COLLECTION_CLASS(ImageDecoderReadRequest)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ImageDecoderReadRequest, ReadRequest)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDecoder)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReader)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ImageDecoderReadRequest, ReadRequest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDecoder)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
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
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::Initialize\n", this);
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
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::Destroy\n", this);
  if (mSourceBuffer) {
    if (!mSourceBuffer->IsComplete()) {
      mSourceBuffer->Complete(NS_ERROR_ABORT);
    }
    mSourceBuffer = nullptr;
  }

  if (mReader) {
    if (CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get()) {
      ErrorResult rv;
      rv.ThrowAbortError("ImageDecoderReadRequest destroyed");

      JSContext* cx = ccjscx->Context();
      JS::Rooted<JS::Value> errorValue(cx);
      if (ToJSValue(cx, std::move(rv), &errorValue)) {
        IgnoredErrorResult ignoredRv;
        if (RefPtr<Promise> p =
                MOZ_KnownLive(mReader)->Cancel(cx, errorValue, ignoredRv)) {
          bool setHandled = NS_WARN_IF(p->SetAnyPromiseIsHandled());
          (void)setHandled;
        }
      }

      JS_ClearPendingException(cx);
    }

    mReader = nullptr;
  }

  mDecoder = nullptr;
}

void ImageDecoderReadRequest::QueueRead() {
  printf_stderr("[AO] [%p] QueueRead\n", this);
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

  if (NS_WARN_IF(!CycleCollectedJSContext::Get())) {
    Complete(NS_ERROR_FAILURE);
    return;
  }

  auto task = MakeRefPtr<ReadRunnable>(this);
  NS_DispatchToCurrentThread(task.forget());
}

void ImageDecoderReadRequest::Read() {
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::Read\n", this);
  if (!mReader) {
    return;
  }

  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
  if (NS_WARN_IF(!ccjscx)) {
    Complete(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<ImageDecoderReadRequest> self(this);
  RefPtr<ReadableStreamDefaultReader> reader(mReader);

  IgnoredErrorResult err;
  reader->ReadChunk(ccjscx->Context(), *self, err);
  if (NS_WARN_IF(err.Failed())) {
    Complete(NS_ERROR_FAILURE);
  }
}

void ImageDecoderReadRequest::Complete(nsresult aErr) {
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::Complete\n", this);
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
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::ImageDecoderReadRequest::ChunkSteps\n", this);
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
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::CloseSteps\n", this);
  Complete(NS_OK);
}

void ImageDecoderReadRequest::ErrorSteps(JSContext* aCx,
                                         JS::Handle<JS::Value> aError,
                                         ErrorResult& aRv) {
  printf_stderr("[AO] [%p] ImageDecoderReadRequest::ErrorSteps\n", this);
  Complete(NS_ERROR_FAILURE);
}

}  // namespace mozilla::dom
