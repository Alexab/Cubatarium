#pragma once

#include "Blocks/BlockDefinition.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

/// Global underwater fog only when the surface map is unavailable.
inline bool ShouldUseGlobalUnderwaterFog(bool camera_submerged, bool map_ready)
{
  return camera_submerged && !map_ready;
}

/// Per-column underwater fog when the surface map is ready near fluids.
inline bool ShouldUsePerColumnUnderwaterFog(bool map_ready, bool nearby_fluid)
{
  return map_ready && nearby_fluid;
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

inline bool IsPartialSubmerge(float eye_y, float surface_y)
{
  constexpr float k_eye_band = 0.25f;
  return std::abs(eye_y - surface_y) < k_eye_band;
}

/// Skip underwater fog on shallow land puddles when the camera is above the surface.
inline bool ShouldApplyUnderwaterFogToColumn(bool camera_submerged,
                                             bool partial_submerge,
                                             int surface_block_y,
                                             int bottom_block_y)
{
  if (camera_submerged || partial_submerge)
  {
    return true;
  }
  if (IsShallowFluidSpan(surface_block_y, bottom_block_y))
  {
    return false;
  }
  return true;
}

/// Per-column tint targets opaque solids only; fluid/cross keep their own look.
/// `alpha_cutout` means a dedicated cutout-only pass — not "alpha discard"
/// shader mode on a merged solid+cutout draw (GPF5).
inline bool ShouldApplyBelowSurfaceFogToPass(bool transparent_pass,
                                             bool alpha_cutout = false)
{
  return !transparent_pass && !alpha_cutout;
}

/// Solid block is directly under a contiguous fluid span in its column.
inline bool ShouldTintBlockBelowFluidColumn(int block_y, int bottom_block_y,
                                            int surface_block_y)
{
  if (surface_block_y < bottom_block_y)
  {
    return false;
  }
  return block_y + 1 >= bottom_block_y && block_y <= surface_block_y;
}

/// Per-fragment underwater fog block policy when the camera is above water.
inline bool ShouldApplyUnderwaterFogToBlock(int block_y, int bottom_block_y,
                                            int surface_block_y)
{
  return ShouldTintBlockBelowFluidColumn(block_y, bottom_block_y,
                                         surface_block_y);
}

/// NDC Y (0=bottom, 1=top) of the fluid surface plane on screen; -2 = disabled.
inline float ComputeScreenWaterlineNdc(const glm::vec3 &eye, float surface_y,
                                       const glm::mat3 &inv_view_rot)
{
  constexpr float k_disabled = -2.0f;
  if (surface_y > 1e8f)
  {
    return k_disabled;
  }

  const bool eye_above_surface = eye.y > surface_y;
  float dividing = k_disabled;

  for (int i = 0; i <= 64; ++i)
  {
    const float tex_y = static_cast<float>(i) / 64.0f;
    const glm::vec3 dir_view =
        glm::normalize(glm::vec3(0.0f, tex_y * 2.0f - 1.0f, -1.0f));
    const glm::vec3 view_dir = inv_view_rot * dir_view;
    if (std::abs(view_dir.y) < 1e-5f)
    {
      continue;
    }
    const float t = (surface_y - eye.y) / view_dir.y;
    if (t <= 0.0f)
    {
      continue;
    }
    if (eye_above_surface)
    {
      if (view_dir.y < 0.0f)
      {
        dividing = tex_y;
      }
    }
    else if (view_dir.y > 0.0f)
    {
      if (dividing < -1.5f || tex_y < dividing)
      {
        dividing = tex_y;
      }
    }
  }

  return dividing;
}

} // namespace cutum
