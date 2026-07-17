#pragma once

#include "World/Core/FluidSurfaceScanTuning.h"

namespace cutum
{

struct URenderFogSettings
{
  static constexpr float NoSurfaceSentinel = -1000.0f;
  static constexpr int FluidSurfaceScanUp = FluidSurfaceScanTuning::ScanUp;
  static constexpr int FluidSurfaceScanDown = FluidSurfaceScanTuning::ScanDown;
  static constexpr int FluidSurfaceWindowMoveThreshold = 16;
  static constexpr int MaxFluidShaderSlots = 3;
};

} // namespace cutum
