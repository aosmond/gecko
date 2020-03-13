/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "Common.h"
#include "Decoder.h"
#include "DecoderFactory.h"
#include "IDecodingTask.h"
#include "mozilla/RefPtr.h"
#include "ProgressTracker.h"
#include "SourceBuffer.h"

#include "mozilla/gfx/SourceSurfaceRawData.h"
#include "mozilla/image/nsJPEGEncoder.h"
#include "mozilla/image/nsPNGEncoder.h"
#include "mozilla/image/nsBMPEncoder.h"
#include "mozilla/image/nsICOEncoder.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

namespace {

static already_AddRefed<DataSourceSurface> GetSurfaceToEncode(
    DecoderType aType, SurfaceFlags aFlags, nsIInputStream* aInputStream,
    SurfaceFormat aOutFormat) {
  // Figure out how much data we have.
  uint64_t length;
  nsresult rv = aInputStream->Available(&length);
  ASSERT_TRUE_OR_RETURN(NS_SUCCEEDED(rv), nullptr);

  // Write the data into a SourceBuffer.
  auto sourceBuffer = MakeNotNull<RefPtr<SourceBuffer>>();
  sourceBuffer->ExpectLength(length);
  rv = sourceBuffer->AppendFromInputStream(aInputStream, length);
  ASSERT_TRUE_OR_RETURN(NS_SUCCEEDED(rv), nullptr);
  sourceBuffer->Complete(NS_OK);

  // Create a decoder.
  RefPtr<Decoder> decoder = DecoderFactory::CreateAnonymousDecoder(
      aType, sourceBuffer, Nothing(), DecoderFlags::FIRST_FRAME_ONLY, aFlags);
  ASSERT_TRUE_OR_RETURN(decoder != nullptr, nullptr);
  RefPtr<IDecodingTask> task =
      new AnonymousDecodingTask(WrapNotNull(decoder), /* aResumable */ false);

  // Run the full decoder synchronously.
  task->Run();

  // Extract the result as a data surface.
  RawAccessFrameRef currentFrame = decoder->GetCurrentFrameRef();
  ASSERT_TRUE_OR_RETURN(!!currentFrame, nullptr);

  RefPtr<SourceSurface> surface = currentFrame->GetSourceSurface();
  ASSERT_TRUE_OR_RETURN(surface != nullptr, nullptr);

  RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
  ASSERT_TRUE_OR_RETURN(dataSurface != nullptr, nullptr);

  // Perform a swizzle to the desired output form as necessary.
  if (aOutFormat == SurfaceFormat::UNKNOWN ||
      dataSurface->GetFormat() == aOutFormat) {
    return dataSurface.forget();
  }

  IntSize size = dataSurface->GetSize();
  SurfaceFormat format = dataSurface->GetFormat();

  // Create a new surface to swizzle the data into.
  auto swizzledSurface = MakeRefPtr<SourceSurfaceAlignedRawData>();
  int32_t outStride = BytesPerPixel(aOutFormat) * size.width;
  bool success = swizzledSurface->Init(size, aOutFormat, true, 0, outStride);
  ASSERT_TRUE_OR_RETURN(success, nullptr);

  DataSourceSurface::ScopedMap wmap(swizzledSurface, DataSourceSurface::WRITE);
  ASSERT_TRUE_OR_RETURN(wmap.IsMapped(), nullptr);

  DataSourceSurface::ScopedMap rmap(dataSurface, DataSourceSurface::READ);
  ASSERT_TRUE_OR_RETURN(rmap.IsMapped(), nullptr);

  // Do the actual swizzle/copy.
  success = SwizzleData(rmap.GetData(), rmap.GetStride(), format,
                        wmap.GetData(), wmap.GetStride(), aOutFormat, size);
  ASSERT_TRUE_OR_RETURN(success, nullptr);

  swizzledSurface->SetFlags(dataSurface->Flags());
  return swizzledSurface.forget();
}

template <typename EncoderType>
void CheckEncoder(DecoderType aDecoderType, SurfaceFormat aFormat,
                  SurfaceFlags aFlags, const BGRAColor& aPrimaryColor,
                  const BGRAColor& aSecondaryColor, const nsAString& aOptions,
                  int32_t aFuzz = 0) {
  RefPtr<DataSourceSurface> expSurface = GenerateSurface(
      IntSize(2, 2), aFormat, aFlags, aPrimaryColor, aSecondaryColor);
  ASSERT_TRUE(expSurface != nullptr);

  DataSourceSurface::ScopedMap map(expSurface, DataSourceSurface::READ);
  ASSERT_TRUE(map.IsMapped());

  auto encoder = MakeRefPtr<EncoderType>();
  nsresult rv = encoder->InitFromSurfaceData(
      map.GetData(), expSurface->GetSize(), map.GetStride(),
      expSurface->GetFormat(), expSurface->Flags(), aOptions);
  ASSERT_EQ(rv, NS_OK);

  RefPtr<DataSourceSurface> gotSurface =
      GetSurfaceToEncode(aDecoderType, aFlags, encoder, aFormat);
  ASSERT_TRUE(gotSurface != nullptr);

  EXPECT_TRUE(MatchSurface(gotSurface, expSurface, aFuzz));
}

class ImageEncoders : public ::testing::Test {
 protected:
  AutoInitializeImageLib mInit;
};

#define IMAGE_GTEST_ENCODER_BASE_F(test_prefix, fuzz)                          \
  TEST_F(ImageEncoders, test_prefix##_RGBX) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::R8G8B8X8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),                    \
        BGRAColor::Green(), EmptyString(), fuzz);                              \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_RGBA) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::R8G8B8A8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE | SurfaceFlags::NO_PREMULTIPLY_ALPHA, \
        BGRAColor::Red(), BGRAColor::Green(), EmptyString(), fuzz);            \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_BGRA) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::B8G8R8A8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE | SurfaceFlags::NO_PREMULTIPLY_ALPHA, \
        BGRAColor::Red(), BGRAColor::Green(), EmptyString(), fuzz);            \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_BGRX) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::B8G8R8X8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),                    \
        BGRAColor::Green(), EmptyString(), fuzz);                              \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_ARGB) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::A8R8G8B8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE | SurfaceFlags::NO_PREMULTIPLY_ALPHA, \
        BGRAColor::Red(), BGRAColor::Green(), EmptyString(), fuzz);            \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_XRGB) {                                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::X8R8G8B8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),                    \
        BGRAColor::Green(), EmptyString(), fuzz);                              \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_RGB) {                                   \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::R8G8B8,                       \
        SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),                    \
        BGRAColor::Green(), EmptyString(), fuzz);                              \
  }

#define IMAGE_GTEST_ENCODER_ALPHA_F(test_prefix, fuzz)                         \
  TEST_F(ImageEncoders, test_prefix##_BGRA_Unpremultiplied) {                  \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::B8G8R8A8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE | SurfaceFlags::NO_PREMULTIPLY_ALPHA, \
        BGRAColor(0, 0, 180, 43), BGRAColor(0, 50, 0, 155), EmptyString(),     \
        fuzz);                                                                 \
  }                                                                            \
                                                                               \
  TEST_F(ImageEncoders, test_prefix##_BGRA_Premultiplied) {                    \
    CheckEncoder<ns##test_prefix##Encoder>(                                    \
        DecoderType::test_prefix, SurfaceFormat::B8G8R8A8,                     \
        SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor(0, 0, 180, 43),            \
        BGRAColor(0, 50, 0, 155), EmptyString(), fuzz);                        \
  }

IMAGE_GTEST_ENCODER_BASE_F(JPEG, /* aFuzz */ 2)
IMAGE_GTEST_ENCODER_BASE_F(PNG, /* aFuzz */ 0)
IMAGE_GTEST_ENCODER_BASE_F(BMP, /* aFuzz */ 0)
IMAGE_GTEST_ENCODER_BASE_F(ICO, /* aFuzz */ 0)

IMAGE_GTEST_ENCODER_ALPHA_F(PNG, /* aFuzz */ 0)
IMAGE_GTEST_ENCODER_ALPHA_F(BMP, /* aFuzz */ 0)
IMAGE_GTEST_ENCODER_ALPHA_F(ICO, /* aFuzz */ 0)

TEST_F(ImageEncoders, JPEG_RGB_Explicit_Quality) {
  CheckEncoder<nsJPEGEncoder>(DecoderType::JPEG, SurfaceFormat::R8G8B8,
                              SurfaceFlags::TO_SRGB_COLORSPACE,
                              BGRAColor::Red(), BGRAColor::Green(),
                              NS_LITERAL_STRING("quality=100"), /* aFuzz */ 1);
}

TEST_F(ImageEncoders, BMP_ARGB_Explicit_24BPP) {
  CheckEncoder<nsBMPEncoder>(DecoderType::BMP, SurfaceFormat::A8R8G8B8,
                             SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),
                             BGRAColor::Green(), NS_LITERAL_STRING("bpp=24"),
                             /* aFuzz */ 0);
}

TEST_F(ImageEncoders, BMP_ARGB_Explicit_24BPP_Version3) {
  CheckEncoder<nsBMPEncoder>(
      DecoderType::BMP, SurfaceFormat::A8R8G8B8,
      SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(), BGRAColor::Green(),
      NS_LITERAL_STRING("bpp=24;version=3"), /* aFuzz */ 0);
}

TEST_F(ImageEncoders, BMP_ARGB_Explicit_24BPP_Version5) {
  CheckEncoder<nsBMPEncoder>(
      DecoderType::BMP, SurfaceFormat::A8R8G8B8,
      SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(), BGRAColor::Green(),
      NS_LITERAL_STRING("bpp=24;version=5"), /* aFuzz */ 0);
}

TEST_F(ImageEncoders, BMP_RGB_Explicit_32BPP) {
  CheckEncoder<nsBMPEncoder>(DecoderType::BMP, SurfaceFormat::R8G8B8,
                             SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),
                             BGRAColor::Green(), NS_LITERAL_STRING("bpp=32"),
                             /* aFuzz */ 0);
}

TEST_F(ImageEncoders, ICO_RGBA_Explicit_PNG) {
  CheckEncoder<nsICOEncoder>(DecoderType::ICO, SurfaceFormat::R8G8B8A8,
                             SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),
                             BGRAColor::Green(),
                             NS_LITERAL_STRING("format=png"), /* aFuzz */ 0);
}

TEST_F(ImageEncoders, ICO_RGBA_Explicit_BMP) {
  CheckEncoder<nsICOEncoder>(DecoderType::ICO, SurfaceFormat::R8G8B8A8,
                             SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(),
                             BGRAColor::Green(),
                             NS_LITERAL_STRING("format=bmp"), /* aFuzz */ 0);
}

TEST_F(ImageEncoders, ICO_RGBA_Explicit_BMP_24BPP) {
  CheckEncoder<nsICOEncoder>(
      DecoderType::ICO, SurfaceFormat::R8G8B8A8,
      SurfaceFlags::TO_SRGB_COLORSPACE, BGRAColor::Red(), BGRAColor::Green(),
      NS_LITERAL_STRING("format=bmp;bpp=24"), /* aFuzz */ 0);
}

TEST_F(ImageEncoders, ICO_RGB_Explicit_BMP_32BPP) {
  CheckEncoder<nsICOEncoder>(
      DecoderType::ICO, SurfaceFormat::R8G8B8, SurfaceFlags::TO_SRGB_COLORSPACE,
      BGRAColor::Red(), BGRAColor::Green(),
      NS_LITERAL_STRING("format=bmp;bpp=32"), /* aFuzz */ 0);
}

}  // namespace
