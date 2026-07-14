#include "WorldGen/Features/ValleyCarver.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

int LocalSurfaceY(int wx, int wz, int trigger_surface_y,
                  const ValleySurfaceYCallback &get_surface_y)
{
  if (get_surface_y)
  {
    return get_surface_y(wx, wz);
  }
  return trigger_surface_y;
}

} // namespace

void CarveColumnValleys(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const ValleyParams &params, int sea_level,
                        float river_width, const ValleySurfaceYCallback &get_surface_y)
{
  if (!params.enabled || params.maxDepth <= 0)
  {
    return;
  }

  const ClimateSample climate = SampleClimate(x, z, seed);
  const float pv = PeaksAndValleys(climate.weirdness);
  const float valley_boost = pv < 0.35f ? 1.25f : 1.0f;
  const float river_noise =
      NormalizedFBM2D(static_cast<float>(x) * 0.008f,
                      static_cast<float>(z) * 0.008f, seed + 8801, 2, 0.5f,
                      2.0f);
  const float width =
      river_width * (1.0f - climate.erosion * 0.5f) * valley_boost;
  const float river_strength =
      Smoothstep(0.50f, 0.72f, (river_noise + 1.0f) * 0.5f) * width;
  if (river_strength <= 0.05f)
  {
    return;
  }

  const float sigma = std::max(0.75f, params.widthSigma);
  const float sigma_sq = sigma * sigma * 2.0f;
  const int max_depth =
      std::max(1, static_cast<int>(static_cast<float>(params.maxDepth) *
                                   river_strength));

  for (int dx = -3; dx <= 3; ++dx)
  {
    for (int dz = -3; dz <= 3; ++dz)
    {
      const int wx = x + dx;
      const int wz = z + dz;
      const float dist_sq = static_cast<float>(dx * dx + dz * dz);
      const float radial = std::exp(-dist_sq / sigma_sq);
      int column_depth =
          static_cast<int>(static_cast<float>(max_depth) * radial);
      if (column_depth <= 0)
      {
        continue;
      }

      const int column_surface_y =
          LocalSurfaceY(wx, wz, surface_y, get_surface_y);
      if (column_surface_y <= sea_level + 2)
      {
        column_depth = static_cast<int>(
            static_cast<float>(column_depth) * params.aquaticDepthScale);
        column_depth = std::max(1, column_depth);
      }

      int carved_bottom = column_surface_y;
      for (int dy = 0; dy < column_depth; ++dy)
      {
        const float dy_profile =
            1.0f - Smoothstep(0.0f, 1.0f,
                              static_cast<float>(dy) /
                                  static_cast<float>(std::max(1, column_depth)));
        if (dy_profile <= 0.05f)
        {
          continue;
        }
        const int y = column_surface_y - dy;
        if (y < 1)
        {
          break;
        }
        carved_bottom = y;
        const glm::ivec3 pos(wx, y, wz);
        if (!ctx.World.IsAir(pos))
        {
          ctx.World.SetBlock(pos, BLOCK_AIR);
        }
      }
      ctx.AccumulateDirtyColumn(std::max(1, carved_bottom), column_surface_y);
    }
  }
}

} // namespace cutum
