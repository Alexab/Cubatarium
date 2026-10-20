#pragma once

#include "WorldGen/Core/Noise.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cutum
{

/// Continuous, warped zero-contour field used to form meandering riverbeds.
/// The signed noise contour keeps channels narrow and connected; warping moves
/// the contour gradually so it does not read as a straight grid or round pits.
inline float SampleRiverChannelStrength(int x, int z, uint32_t seed,
                                        float noise_scale, float river_width,
                                        float erosion, float weirdness,
                                        float warp_blocks = 10.0f)
{
  const float scale = std::clamp(noise_scale, 0.001f, 0.05f);
  const float wx = static_cast<float>(x);
  const float wz = static_cast<float>(z);
  const float warp = NormalizedFBM2D(
      wx * scale * 0.38f + 17.0f, wz * scale * 0.38f - 29.0f,
      seed + 8813U, 2, 0.5f, 2.0f);
  const float warp_offset = warp * warp_blocks;
  const float field = NormalizedFBM2D(
      (wx + warp_offset) * scale, (wz - warp_offset * 0.71f) * scale,
      seed + 8801U, 3, 0.5f, 2.0f);

  const float valley_factor = PeaksAndValleys(weirdness) < 0.35f ? 1.2f : 1.0f;
  const float erosion_factor = std::clamp(1.15f - erosion * 0.3f, 0.75f, 1.15f);
  const float width = std::clamp(river_width, 0.5f, 1.5f) * erosion_factor *
                      valley_factor;
  const float threshold = std::clamp(scale * width * 12.0f, 0.012f, 0.12f);
  return std::clamp(
      1.0f - Smoothstep(threshold * 0.28f, threshold, std::fabs(field)),
      0.0f, 1.0f);
}

} // namespace cutum
