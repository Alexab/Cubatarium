#pragma once

#include "Blocks/BlockDefinition.h"

namespace cutum
{

/// Full-screen underwater fog whenever the camera is submerged.
inline bool ShouldUseGlobalUnderwaterFog(bool camera_in_fluid, bool /*map_ready*/)
{
  return camera_in_fluid;
}

/// Per-column below-surface tint when the surface map is ready near fluids.
inline bool ShouldUsePerColumnBelowSurfaceFog(bool map_ready,
                                              bool nearby_fluid)
{
  return map_ready && nearby_fluid;
}

/// Submerged: light depth supplement with global fog. Wading above water: off.
inline float BelowSurfaceFogStrength(bool map_ready, bool camera_in_fluid)
{
  if (!map_ready || !camera_in_fluid)
  {
    return 0.0f;
  }
  return 0.25f;
}

/// TD-FL-034 v2: treat capsule feet below surface as submerged for fog transitions.
inline bool IsCameraSubmergedForFog(bool column_valid, float eye_y,
                                    float surface_y, float capsule_feet_y,
                                    bool below_surface_fog_v2)
{
  if (!column_valid)
  {
    return false;
  }
  if (eye_y < surface_y)
  {
    return true;
  }
  return below_surface_fog_v2 && capsule_feet_y < surface_y - 1e-4f;
}

/// Skip per-column tint in the shallow band when viewing from above water.
inline float BelowSurfaceFogDepthMin(bool camera_in_fluid)
{
  return camera_in_fluid ? 0.0f : 0.5f;
}

/// Per-column tint targets opaque solids only; fluid/cutout/cross keep their own look.
inline bool ShouldApplyBelowSurfaceFogToPass(bool transparent_pass,
                                             bool alpha_cutout = false)
{
  return !transparent_pass && !alpha_cutout;
}

/// Cross vegetation and wading side faces are excluded from column tint.
inline bool ShouldApplyBelowSurfaceFogToFace(bool camera_in_fluid, int face_index)
{
  constexpr int kCrossFaceIndex = 127;
  if (face_index == kCrossFaceIndex)
  {
    return false;
  }
  if (!camera_in_fluid)
  {
    return face_index == 4;
  }
  return true;
}

/// Horizontal top faces keep the shallow band when wading above water.
inline float BelowSurfaceFogDepthMinForFace(bool camera_in_fluid, int face_index)
{
  if (!camera_in_fluid && face_index != 4)
  {
    return 0.0f;
  }
  return BelowSurfaceFogDepthMin(camera_in_fluid);
}

/// Solid block is directly under a contiguous fluid span in its column.
inline bool ShouldTintBlockBelowFluidColumn(int block_y, int bottom_block_y,
                                            int surface_block_y)
{
  if (surface_block_y < bottom_block_y)
  {
    return false;
  }
  return block_y + 1 >= bottom_block_y && block_y < surface_block_y;
}

inline float SubmergedBelowSurfaceFogMin(const FluidViewProfile &profile)
{
  return profile.FogMinBlend > profile.BelowSurfaceFogMin
             ? profile.FogMinBlend
             : profile.BelowSurfaceFogMin;
}

} // namespace cutum
