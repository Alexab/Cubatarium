#pragma once

#include "Render/Engine/RenderFogSettings.h"
#include <cstdlib>

namespace cutum
{

inline bool ShouldRefreshFluidSurfaceWindow(int old_origin_x, int old_origin_z,
                                            int new_origin_x, int new_origin_z)
{
  return std::abs(new_origin_x - old_origin_x) >=
             URenderFogSettings::FluidSurfaceWindowMoveThreshold ||
         std::abs(new_origin_z - old_origin_z) >=
             URenderFogSettings::FluidSurfaceWindowMoveThreshold;
}

inline float FluidSurfaceStagingSentinel()
{
  return URenderFogSettings::NoSurfaceSentinel;
}

} // namespace cutum
