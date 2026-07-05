#pragma once

#include "World/Core/RuntimeTuning.h"
#include <cstdlib>

namespace cutum
{

inline bool ShouldRefreshFluidSurfaceWindow(int old_origin_x, int old_origin_z,
                                            int new_origin_x, int new_origin_z)
{
  const int threshold =
      URuntimeTuning::Get().FluidSurfaceWindowMoveThreshold;
  return std::abs(new_origin_x - old_origin_x) >= threshold ||
         std::abs(new_origin_z - old_origin_z) >= threshold;
}

inline float FluidSurfaceStagingSentinel()
{
  return -1000.0f;
}

} // namespace cutum
