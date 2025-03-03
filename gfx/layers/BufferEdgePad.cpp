#include "BufferEdgePad.h"

#include "mozilla/gfx/Point.h" // for IntSize
#include "mozilla/gfx/Types.h" // for SurfaceFormat

namespace mozilla {
namespace layers {

using namespace gfx;

void
PadDrawTargetOutFromRegion(RefPtr<DrawTarget> aDrawTarget, nsIntRegion &aRegion)
{
  struct LockedBits {
    uint8_t *data;
    IntSize size;
    int32_t stride;
    SurfaceFormat format;
    static int clamp(int x, int min, int max)
    {
      if (x < min)
        x = min;
      if (x > max)
        x = max;
      return x;
    }

    static void ensure_memcpy(uint8_t *dst, uint8_t *src, size_t n, uint8_t *bitmap, int stride, int height)
    {
        if (src + n > bitmap + stride*height) {
            MOZ_CRASH("GFX: long src memcpy");
        }
        if (src < bitmap) {
            MOZ_CRASH("GFX: short src memcpy");
        }
        if (dst + n > bitmap + stride*height) {
            MOZ_CRASH("GFX: long dst mempcy");
        }
        if (dst < bitmap) {
            MOZ_CRASH("GFX: short dst mempcy");
        }
    }

    static void visitor(void *closure, VisitSide side, int x1, int y1, int x2, int y2) {
      LockedBits *lb = static_cast<LockedBits*>(closure);
      uint8_t *bitmap = lb->data;
      const int bpp = gfx::BytesPerPixel(lb->format);
      const int stride = lb->stride;
      const int width = lb->size.width;
      const int height = lb->size.height;

      if (side == VisitSide::TOP) {
        if (y1 > 0) {
          x1 = clamp(x1, 0, width - 1);
          x2 = clamp(x2, 0, width - 1);
          ensure_memcpy(&bitmap[x1*bpp + (y1-1) * stride], &bitmap[x1*bpp + y1 * stride], (x2 - x1) * bpp, bitmap, stride, height);
          memcpy(&bitmap[x1*bpp + (y1-1) * stride], &bitmap[x1*bpp + y1 * stride], (x2 - x1) * bpp);
        }
      } else if (side == VisitSide::BOTTOM) {
        if (y1 < height) {
          x1 = clamp(x1, 0, width - 1);
          x2 = clamp(x2, 0, width - 1);
          ensure_memcpy(&bitmap[x1*bpp + y1 * stride], &bitmap[x1*bpp + (y1-1) * stride], (x2 - x1) * bpp, bitmap, stride, height);
          memcpy(&bitmap[x1*bpp + y1 * stride], &bitmap[x1*bpp + (y1-1) * stride], (x2 - x1) * bpp);
        }
      } else if (side == VisitSide::LEFT) {
        if (x1 > 0) {
          while (y1 != y2) {
            memcpy(&bitmap[(x1-1)*bpp + y1 * stride], &bitmap[x1*bpp + y1*stride], bpp);
            y1++;
          }
        }
      } else if (side == VisitSide::RIGHT) {
        if (x1 < width) {
          while (y1 != y2) {
            memcpy(&bitmap[x1*bpp + y1 * stride], &bitmap[(x1-1)*bpp + y1*stride], bpp);
            y1++;
          }
        }
      }

    }
  } lb;

  if (aDrawTarget->LockBits(&lb.data, &lb.size, &lb.stride, &lb.format)) {
    // we can only pad software targets so if we can't lock the bits don't pad
    aRegion.VisitEdges(lb.visitor, &lb);
    aDrawTarget->ReleaseBits(lb.data);
  }
}

} // namespace layers
} // namespace mozilla
