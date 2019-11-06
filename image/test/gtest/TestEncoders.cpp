/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "nsString.h"
#include "mozilla/Base64.h"
#include "mozilla/image/nsPNGEncoder.h"

#include "Common.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

namespace {

class ImageEncoders : public ::testing::Test {
 protected:
  AutoInitializeImageLib mInit;
};

static void CheckEncodeResult(imgIEncoder* aEncoder,
                              const nsACString& aExpectedEncoding) {
  nsCString produced;
  nsresult rv = Base64EncodeInputStream(aEncoder, produced, 0, 0);
  EXPECT_EQ(rv, NS_OK);
  EXPECT_TRUE(aExpectedEncoding.Equals(produced));
}

static void CheckEncodeSingleFrame(imgIEncoder* aEncoder,
                                   const uint8_t* aPixels, uint32_t aLength,
                                   uint32_t aWidth, uint32_t aHeight,
                                   uint32_t aFormat, const nsAString& aOptions,
                                   const nsACString& aExpectedEncoding) {
  uint32_t stride =
      aFormat == imgIEncoder::INPUT_FORMAT_RGB ? 3 * aWidth : 4 * aWidth;
  nsresult rv = aEncoder->InitFromData(aPixels, aLength, aWidth, aHeight,
                                       stride, aFormat, aOptions);
  EXPECT_EQ(rv, NS_OK);

  CheckEncodeResult(aEncoder, aExpectedEncoding);
}

static void CheckEncodeStart(imgIEncoder* aEncoder, uint32_t aWidth,
                             uint32_t aHeight, uint32_t aFormat,
                             const nsAString& aOptions) {
  nsresult rv = aEncoder->StartImageEncode(aWidth, aHeight, aFormat, aOptions);
  EXPECT_EQ(rv, NS_OK);
}

static void CheckEncodeFinish(imgIEncoder* aEncoder,
                              const nsACString& aExpectedEncoding) {
  nsresult rv = aEncoder->EndImageEncode();
  EXPECT_EQ(rv, NS_OK);

  CheckEncodeResult(aEncoder, aExpectedEncoding);
}

static void CheckEncodeFrame(imgIEncoder* aEncoder, const uint8_t* aPixels,
                             uint32_t aLength, uint32_t aWidth,
                             uint32_t aHeight, uint32_t aFormat,
                             const nsAString& aOptions) {
  uint32_t stride =
      aFormat == imgIEncoder::INPUT_FORMAT_RGB ? 3 * aWidth : 4 * aWidth;
  nsresult rv = aEncoder->AddImageFrame(aPixels, aLength, aWidth, aHeight,
                                        stride, aFormat, aOptions);
  EXPECT_EQ(rv, NS_OK);
}

TEST_F(ImageEncoders, PNG_RGB_DefaultTransparency) {
  // A 3x3 image, rows are red, green, blue.
  // RGB format, transparency defaults.
  uint8_t pixels[] = {255, 0, 0,   255, 0, 0, 255, 0, 0, 0,   255, 0, 0,  255,
                      0,   0, 255, 0,   0, 0, 255, 0, 0, 255, 0,   0, 255};

  NS_NAMED_LITERAL_CSTRING(expected,
                           "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAIAAADZSiLoAAAAEUl"
                           "EQVQImWP4z8AAQTAamQkAhpcI+DeMzFcAAAAASUVORK5CYII=");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeSingleFrame(encoder, pixels, sizeof(pixels), 3, 3,
                         imgIEncoder::INPUT_FORMAT_RGB, NS_LITERAL_STRING(""),
                         expected);
}

TEST_F(ImageEncoders, PNG_RGB_NoTransparency) {
  // A 3x3 image, rows are red, green, blue.
  // RGB format, transparency=none.
  uint8_t pixels[] = {255, 0, 0,   255, 0, 0, 255, 0, 0, 0,   255, 0, 0,  255,
                      0,   0, 255, 0,   0, 0, 255, 0, 0, 255, 0,   0, 255};

  NS_NAMED_LITERAL_CSTRING(expected,
                           "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAIAAADZSiLoAAAAEUl"
                           "EQVQImWP4z8AAQTAamQkAhpcI+DeMzFcAAAAASUVORK5CYII=");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeSingleFrame(encoder, pixels, sizeof(pixels), 3, 3,
                         imgIEncoder::INPUT_FORMAT_RGB,
                         NS_LITERAL_STRING("transparency=none"), expected);
}

TEST_F(ImageEncoders, PNG_RGBA_DefaultTransparency) {
  // A 3x3 image, rows are: red, green, blue. Columns are: 0%, 33%, 66%
  // transparent.
  uint8_t pixels[] = {255, 0,   0,   255, 255, 0,   0,   170, 255, 0, 0,  85, 0,
                      255, 0,   255, 0,   255, 0,   170, 0,   255, 0, 85, 0,  0,
                      255, 255, 0,   0,   255, 170, 0,   0,   255, 85};

  NS_NAMED_LITERAL_CSTRING(expected,
                           "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
                           "AAAAIElEQVQImSXJMQEAMAwEIUy+"
                           "yZi8DmVFFBcjycn86GgPJw4O8v9DkkEAAAAASUVORK5CYII=");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeSingleFrame(encoder, pixels, sizeof(pixels), 3, 3,
                         imgIEncoder::INPUT_FORMAT_RGBA, NS_LITERAL_STRING(""),
                         expected);
}

TEST_F(ImageEncoders, PNG_RGBA_NoTransparency) {
  // A 3x3 image, rows are: red, green, blue. Columns are: 0%, 33%, 66%
  // transparent.
  uint8_t pixels[] = {255, 0,   0,   255, 255, 0,   0,   170, 255, 0, 0,  85, 0,
                      255, 0,   255, 0,   255, 0,   170, 0,   255, 0, 85, 0,  0,
                      255, 255, 0,   0,   255, 170, 0,   0,   255, 85};

  NS_NAMED_LITERAL_CSTRING(expected,
                           "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAIAAADZSiLoAAAAEUl"
                           "EQVQImWP4z8AAQTAamQkAhpcI+DeMzFcAAAAASUVORK5CYII=");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeSingleFrame(encoder, pixels, sizeof(pixels), 3, 3,
                         imgIEncoder::INPUT_FORMAT_RGBA,
                         NS_LITERAL_STRING("transparency=none"), expected);
}

TEST_F(ImageEncoders, APNG_RGB_FullFrames) {
  // A 3x3 image with 3 frames, alternating red, green, blue. RGB format.
  uint8_t frame1[] = {
      255, 0,   0, 255, 0,   0, 255, 0,   0, 255, 0,   0, 255, 0,
      0,   255, 0, 0,   255, 0, 0,   255, 0, 0,   255, 0, 0,
  };

  uint8_t frame2[] = {
      0, 255, 0,   0, 255, 0,   0, 255, 0,   0, 255, 0,   0, 255,
      0, 0,   255, 0, 0,   255, 0, 0,   255, 0, 0,   255, 0,
  };

  uint8_t frame3[] = {
      0,   0, 255, 0,   0, 255, 0,   0, 255, 0,   0, 255, 0,   0,
      255, 0, 0,   255, 0, 0,   255, 0, 0,   255, 0, 0,   255,
  };

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAIAAADZSiLoAAAACGFjVEwAAAADAAAAAM7tusAA"
      "AAAaZmNUTAAAAAAAAAADAAAAAwAAAAAAAAAAAfQD6AAAdRYgGAAAABBJREFUCJlj+M/AAEEM"
      "WFgAj44I+H2CySsAAAAaZmNUTAAAAAEAAAADAAAAAwAAAAAAAAAAAfQD6AAA7mXKzAAAABNm"
      "ZEFUAAAAAgiZY2D4zwBFWFgAhpcI+I731VcAAAAaZmNUTAAAAAMAAAADAAAAAwAAAAAAAAAA"
      "AfQD6AAAA/"
      "MZJQAAABNmZEFUAAAABAiZY2Bg+A9DmCwAfaAI+AGmQVoAAAAASUVORK5CYII=");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGB,
                   NS_LITERAL_STRING("frames=3;skipfirstframe=no;plays=0"));
  CheckEncodeFrame(encoder, frame1, sizeof(frame1), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGB,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame2, sizeof(frame2), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGB,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame3, sizeof(frame3), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGB,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFinish(encoder, expected);
}

TEST_F(ImageEncoders, APNG_RGBA_FullFrames) {
  // A 3x3 image with 3 frames, alternating red, green, blue. RGBA format.
  uint8_t frame1[] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

  uint8_t frame2[] = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                      0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                      0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};

  uint8_t frame3[] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
      "AAAACGFjVEwAAAADAAAAAM7tusAAAAAaZmNUTAAAAAAAAAADAAAAAwAAAAAAAAAAAfQD6AAA"
      "dRYgGAAAABFJREFUCJlj+M/A8B+GGXByAF3XEe/"
      "CoiJ1AAAAGmZjVEwAAAABAAAAAwAAAAMAAAAAAAAAAAH0A+"
      "gAAO5lyswAAAASZmRBVAAAAAIImWNg+"
      "I8EcXIAVOAR77Vyl9QAAAAaZmNUTAAAAAMAAAADAAAAAwAAAAAAAAAAAfQD6AAAA/"
      "MZJQAAABRmZEFUAAAABAiZY2Bg+P8fgXFxAEvpEe8rClxSAAAAAElFTkSuQmCC");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("frames=3;skipfirstframe=no;plays=0"));
  CheckEncodeFrame(encoder, frame1, sizeof(frame1), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame2, sizeof(frame2), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame3, sizeof(frame3), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFinish(encoder, expected);
}

TEST_F(ImageEncoders, APNG_RGBA_SkipFirstFrame) {
  // A 3x3 image with 3 frames, alternating red, green, blue. RGBA format.
  // The first frame is skipped, so it will only flash green/blue (or static red
  // in an APNG-unaware viewer)
  uint8_t frame1[] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

  uint8_t frame2[] = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                      0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,
                      0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};

  uint8_t frame3[] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
      "AAAACGFjVEwAAAACAAAAAPONk3AAAAARSURBVAiZY/"
      "jPwPAfhhlwcgBd1xHvwqIidQAAABpmY1RMAAAAAAAAAAMAAAADAAAAAAAAAAAB9APoAAB1Fi"
      "AYAAAAEmZkQVQAAAABCJljYPiPBHFyAFTgEe+kD/"
      "2tAAAAGmZjVEwAAAACAAAAAwAAAAMAAAAAAAAAAAH0A+gAAJiA8/"
      "EAAAAUZmRBVAAAAAMImWNgYPj/H4FxcQBL6RHvC5ggGQAAAABJRU5ErkJggg==");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("frames=3;skipfirstframe=yes;plays=0"));
  CheckEncodeFrame(encoder, frame1, sizeof(frame1), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame2, sizeof(frame2), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame3, sizeof(frame3), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFinish(encoder, expected);
}

TEST_F(ImageEncoders, APNG_RGBA_BlendOver) {
  // A 3x3 image with 3 frames, alternating red, green, blue. RGBA format.
  // blend = over mode
  // (The green frame is a horizontal gradient, and the blue frame is a
  // vertical gradient. They stack as they animate.)
  uint8_t frame1[] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

  uint8_t frame2[] = {0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75,
                      0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75,
                      0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75};

  uint8_t frame3[] = {0, 0, 255, 75,  0, 0, 255, 75,  0, 0, 255, 75,
                      0, 0, 255, 180, 0, 0, 255, 180, 0, 0, 255, 180,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
      "AAAACGFjVEwAAAADAAAAAM7tusAAAAAaZmNUTAAAAAAAAAADAAAAAwAAAAAAAAAAAfQD6AAA"
      "dRYgGAAAABFJREFUCJlj+M/A8B+GGXByAF3XEe/"
      "CoiJ1AAAAGmZjVEwAAAABAAAAAwAAAAMAAAAAAAAAAAH0A+gAAZli+"
      "loAAAAcZmRBVAAAAAIImWNg+M/wn+E/wxaG/"
      "wzeDDg5ACeGDvKVa3pyAAAAGmZjVEwAAAADAAAAAwAAAAMAAAAAAAAAAAH0A+"
      "gAAXT0KbMAAAAcZmRBVAAAAAQImWNgYPjvjcAM/7cgMMP//"
      "zAMAPqkDvLn1m3SAAAAAElFTkSuQmCC");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("frames=3;skipfirstframe=no;plays=0"));
  CheckEncodeFrame(encoder, frame1, sizeof(frame1), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame2, sizeof(frame2), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=over"));
  CheckEncodeFrame(encoder, frame3, sizeof(frame3), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=over"));
  CheckEncodeFinish(encoder, expected);
}

TEST_F(ImageEncoders, APNG_RGBA_BlendOver_DisposeBackground) {
  // A 3x3 image with 3 frames, alternating red, green, blue. RGBA format.
  // blend = over, dispose = background
  // (The green frame is a horizontal gradient, and the blue frame is a
  // vertical gradient. Each frame is displayed individually, blended to
  // whatever the background is.)
  uint8_t frame1[] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

  uint8_t frame2[] = {0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75,
                      0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75,
                      0, 255, 0, 255, 0, 255, 0, 180, 0, 255, 0, 75};

  uint8_t frame3[] = {0, 0, 255, 75,  0, 0, 255, 75,  0, 0, 255, 75,
                      0, 0, 255, 180, 0, 0, 255, 180, 0, 0, 255, 180,
                      0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
      "AAAACGFjVEwAAAADAAAAAM7tusAAAAAaZmNUTAAAAAAAAAADAAAAAwAAAAAAAAAAAfQD6AEA"
      "bA0RWQAAABFJREFUCJlj+M/A8B+GGXByAF3XEe/"
      "CoiJ1AAAAGmZjVEwAAAABAAAAAwAAAAMAAAAAAAAAAAH0A+"
      "gBAYB5yxsAAAAcZmRBVAAAAAIImWNg+M/wn+E/wxaG/"
      "wzeDDg5ACeGDvKVa3pyAAAAGmZjVEwAAAADAAAAAwAAAAMAAAAAAAAAAAH0A+"
      "gBAW3vGPIAAAAcZmRBVAAAAAQImWNgYPjvjcAM/7cgMMP//"
      "zAMAPqkDvLn1m3SAAAAAElFTkSuQmCC");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("frames=3;skipfirstframe=no;plays=0"));
  CheckEncodeFrame(
      encoder, frame1, sizeof(frame1), 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
      NS_LITERAL_STRING("delay=500;dispose=background;blend=source"));
  CheckEncodeFrame(
      encoder, frame2, sizeof(frame2), 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
      NS_LITERAL_STRING("delay=500;dispose=background;blend=over"));
  CheckEncodeFrame(
      encoder, frame3, sizeof(frame3), 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
      NS_LITERAL_STRING("delay=500;dispose=background;blend=over"));
  CheckEncodeFinish(encoder, expected);
}

TEST_F(ImageEncoders, APNG_RGBA_DiagonalLine) {
  // A 3x3 image with 4 frames. First frame is white, then 1x1 frames draw a
  // diagonal line
  uint8_t frame1[] = {255, 255, 255, 255, 255, 255, 255, 255, 255,
                      255, 255, 255, 255, 255, 255, 255, 255, 255,
                      255, 255, 255, 255, 255, 255, 255, 255, 255,
                      255, 255, 255, 255, 255, 255, 255, 255, 255};

  uint8_t frame2[] = {0, 0, 0, 255};

  NS_NAMED_LITERAL_CSTRING(
      expected,
      "iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/"
      "AAAACGFjVEwAAAAEAAAAAHzNZtAAAAAaZmNUTAAAAAAAAAADAAAAAwAAAAAAAAAAAfQD6AAA"
      "dRYgGAAAAA5JREFUCJlj+"
      "I8EGHByALuHI91kueRqAAAAGmZjVEwAAAABAAAAAQAAAAEAAAAAAAAAAAH0A+"
      "gAADJXfawAAAARZmRBVAAAAAIImWNgYGD4DwABBAEAbr5mpgAAABpmY1RMAAAAAwAAAAEAAA"
      "ABAAAAAQAAAAEB9APoAAC4OHoxAAAAEWZkQVQAAAAECJljYGBg+"
      "A8AAQQBAJZ8LRAAAAAaZmNUTAAAAAUAAAABAAAAAQAAAAIAAAACAfQD6AAA/"
      "fh01wAAABFmZEFUAAAABgiZY2BgYPgPAAEEAQB3Eum9AAAAAElFTkSuQmCC");

  auto encoder = MakeRefPtr<nsPNGEncoder>();
  CheckEncodeStart(encoder, 3, 3, imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("frames=4;skipfirstframe=no;plays=0"));
  CheckEncodeFrame(encoder, frame1, sizeof(frame1), 3, 3,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(encoder, frame2, sizeof(frame2), 1, 1,
                   imgIEncoder::INPUT_FORMAT_RGBA,
                   NS_LITERAL_STRING("delay=500;dispose=none;blend=source"));
  CheckEncodeFrame(
      encoder, frame2, sizeof(frame2), 1, 1, imgIEncoder::INPUT_FORMAT_RGBA,
      NS_LITERAL_STRING(
          "delay=500;dispose=none;blend=source;xoffset=1;yoffset=1"));
  CheckEncodeFrame(
      encoder, frame2, sizeof(frame2), 1, 1, imgIEncoder::INPUT_FORMAT_RGBA,
      NS_LITERAL_STRING(
          "delay=500;dispose=none;blend=source;xoffset=2;yoffset=2"));
  CheckEncodeFinish(encoder, expected);
}

}  // namespace
