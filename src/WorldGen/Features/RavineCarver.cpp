#include "WorldGen/Features/RavineCarver.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

uint32_t RavineHash(int x, int z, uint32_t seed)
{
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ (seed + 4400);
}

int ResolveColumnSurfaceY(int wx, int wz, int trigger_surface_y,
                          const RavineSurfaceYCallback &get_surface_y)
{
  if (get_surface_y)
  {
    return get_surface_y(wx, wz);
  }
  return trigger_surface_y;
}

} // namespace

void CarveColumnRavinesDeterministic(WorldGenContext &ctx, int x, int z,
                                       int surface_y,
                                       const RavineParams &params,
                                       int sea_level,
                                       const RavineSurfaceYCallback &get_surface_y)
{
  if (!params.enabled)
  {
    return;
  }

  const int carve_depth = params.maxDepth;
  for (int dx = -3; dx <= 3; ++dx)
  {
    for (int dz = -3; dz <= 3; ++dz)
    {
      const int wx = x + dx;
      const int wz = z + dz;
      const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      const float t = std::clamp(dist / 3.5f, 0.0f, 1.0f);
      const float width_profile = 1.0f - Smoothstep(0.0f, 1.0f, t);
      int column_depth =
          static_cast<int>(static_cast<float>(carve_depth) * width_profile);
      if (column_depth <= 0)
      {
        continue;
      }

      const int column_surface_y =
          ResolveColumnSurfaceY(wx, wz, surface_y, get_surface_y);
      if (column_surface_y <= sea_level + 2 && params.aquaticMaxDepth > 0)
      {
        column_depth =
            std::min(column_depth, std::max(1, params.aquaticMaxDepth));
      }

      int carved_bottom = column_surface_y;
      for (int dy = 0; dy < column_depth; ++dy)
      {
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

void CarveColumnRavines(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const RavineParams &params, int sea_level,
                        const RavineSurfaceYCallback &get_surface_y)
{
  if (!params.enabled || params.rarity <= 0)
  {
    return;
  }
  if (RavineHash(x, z, seed) % static_cast<uint32_t>(params.rarity) != 0)
  {
    return;
  }

  const float ridge =
      1.0f -
      std::fabs(NormalizedFBM2D(static_cast<float>(x) * 0.004f,
                                static_cast<float>(z) * 0.004f, seed + 4400, 3,
                                0.5f, 2.0f));
  const float path =
      NormalizedFBM2D(static_cast<float>(x) * 0.015f,
                      static_cast<float>(z) * 0.015f, seed + 4401, 2, 0.5f,
                      2.0f);
  const float combined = ridge * path;
  if (combined < 0.82f)
  {
    return;
  }

  const float depth_factor = Smoothstep(0.82f, 0.95f, combined);
  RavineParams effective = params;
  effective.maxDepth =
      params.minDepth +
      static_cast<int>(depth_factor *
                       static_cast<float>(params.maxDepth - params.minDepth));
  effective.minDepth = effective.maxDepth;
  CarveColumnRavinesDeterministic(ctx, x, z, surface_y, effective, sea_level,
                                  get_surface_y);
}

} // namespace cutum
