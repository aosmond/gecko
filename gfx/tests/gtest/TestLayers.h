/* vim:set ts=2 sw=2 sts=2 et: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#ifndef GFX_TEST_LAYERS_H
#define GFX_TEST_LAYERS_H

#include "Layers.h"
#include "nsTArray.h"
#include "mozilla/layers/ISurfaceAllocator.h"

namespace mozilla {
namespace layers {

class TestSurfaceAllocator final : public ISurfaceAllocator {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TestSurfaceAllocator, final)

 public:
  TestSurfaceAllocator() = default;

  bool IsSameProcess() const override { return true; }

 private:
  ~TestSurfaceAllocator() override = default;
};

}  // namespace layers
}  // namespace mozilla

#endif
