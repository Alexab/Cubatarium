#pragma once

#include "Blocks/BlockDefinition.h"

namespace cutum
{

/// Full-screen underwater fog whenever the camera is submerged.
inline bool ShouldUseGlobalUnderwaterFog(bool camera_in_fluid, bool /*map_ready*/)
{
  return camera_in_fluid;
}

/// Per-column below-surface tint whenever the surface map is ready.
inline bool ShouldUsePerColumnBelowSurfaceFog(bool map_ready)
{
  return map_ready;
}

/// Wading: full column tint; submerged: light depth supplement with global fog.
inline float BelowSurfaceFogStrength(bool map_ready, bool camera_in_fluid)
{
  if (!map_ready)
  {
    return 0.0f;
  }
  return camera_in_fluid ? 0.25f : 1.0f;
}

/// Skip per-column tint in the shallow band when viewing from above water.
inline float BelowSurfaceFogDepthMin(bool camera_in_fluid)
{
  return camera_in_fluid ? 0.0f : 0.5f;
}

inline float SubmergedBelowSurfaceFogMin(const FluidViewProfile &profile)
{
  return profile.FogMinBlend > profile.BelowSurfaceFogMin
             ? profile.FogMinBlend
             : profile.BelowSurfaceFogMin;
}

} // namespace cutum
