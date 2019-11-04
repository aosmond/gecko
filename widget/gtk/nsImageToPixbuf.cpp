/* vim:set sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "nsImageToPixbuf.h"

#include "imgIContainer.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/RefPtr.h"

using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::SurfaceFormat;

NS_IMPL_ISUPPORTS(nsImageToPixbuf, nsIImageToPixbuf)

inline unsigned char unpremultiply(unsigned char color, unsigned char alpha) {
  if (alpha == 0) return 0;
  // plus alpha/2 to round instead of truncate
  return (color * 255 + alpha / 2) / alpha;
}

NS_IMETHODIMP_(GdkPixbuf*)
nsImageToPixbuf::ConvertImageToPixbuf(imgIContainer* aImage) {
  return ImageToPixbuf(aImage);
}

GdkPixbuf* nsImageToPixbuf::ImageToPixbuf(imgIContainer* aImage) {
  RefPtr<SourceSurface> surface = aImage->GetFrame(
      imgIContainer::FRAME_CURRENT,
      imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY);

  // If the last call failed, it was probably because our call stack originates
  // in an imgINotificationObserver event, meaning that we're not allowed
  // request a sync decode. Presumably the originating event is something
  // sensible like OnStopFrame(), so we can just retry the call without a sync
  // decode.
  if (!surface)
    surface = aImage->GetFrame(imgIContainer::FRAME_CURRENT,
                               imgIContainer::FLAG_NONE);

  NS_ENSURE_TRUE(surface, nullptr);

  return SourceSurfaceToPixbuf(surface, surface->GetSize().width,
                               surface->GetSize().height);
}

GdkPixbuf* nsImageToPixbuf::SourceSurfaceToPixbuf(SourceSurface* aSurface,
                                                  int32_t aWidth,
                                                  int32_t aHeight) {
  MOZ_ASSERT(aWidth <= aSurface->GetSize().width &&
                 aHeight <= aSurface->GetSize().height,
             "Requested rect is bigger than the supplied surface");

  RefPtr<DataSourceSurface> dataSurface = aSurface->GetDataSurface();
  DataSourceSurface::ScopedMap map(dataSurface, DataSourceSurface::READ);
  if (!map.IsMapped()) {
    return nullptr;
  }

  GdkPixbuf* pixbuf =
      gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, aWidth, aHeight);
  if (!pixbuf) {
    return nullptr;
  }

  uint32_t destStride = gdk_pixbuf_get_rowstride(pixbuf);
  guchar* destPixels = gdk_pixbuf_get_pixels(pixbuf);

  SurfaceFormat format = dataSurface->GetFormat();
  if (IsOpaque(format)) {
    SwizzleData(map.GetData(), map.GetStride(), format, destPixels, destStride,
                SurfaceFormat::R8G8B8X8, aSurface->GetSize());
  } else {
    UnpremultiplyData(map.GetData(), map.GetStride(), format, destPixels,
                      destStride, SurfaceFormat::R8G8B8A8, aSurface->GetSize());
  }

  return pixbuf;
}
