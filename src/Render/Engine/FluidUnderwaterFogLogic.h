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

/// Submerged: light depth supplement with global fog. Wading above water: off (v1).
inline float BelowSurfaceFogStrength(bool map_ready, bool camera_in_fluid)
{
  if (!map_ready || !camera_in_fluid)
  {
    return 0.0f;
  }
  return 0.25f;
}

inline bool IsShallowFluidSpan(int surface_block_y, int bottom_block_y)
{
  if (surface_block_y < bottom_block_y)
  {
    return false;
  }
  const int span = surface_block_y - bottom_block_y + 1;
  return span > 0 && span <= 2;
}

inline bool IsPartialSubmerge(bool camera_in_fluid, float eye_y, float surface_y)
{
  if (camera_in_fluid || surface_y <= eye_y)
  {
    return false;
  }
  constexpr float k_eye_band = 0.25f;
  return eye_y > surface_y - k_eye_band;
}

/// v2 shore policy: weak tint above water for deep columns only; no puddle tint on land.
inline float BelowSurfaceFogStrengthV2(bool map_ready, bool camera_in_fluid,
                                     bool partial_submerge, int surface_block_y,
                                     int bottom_block_y)
{
  if (!map_ready)
  {
    return 0.0f;
  }
  if (camera_in_fluid || partial_submerge)
  {
    return partial_submerge ? 0.35f : 0.25f;
  }
  if (IsShallowFluidSpan(surface_block_y, bottom_block_y))
  {
    return 0.0f;
  }
  return 0.12f;
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
