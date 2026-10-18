#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Features/RiverField.h"
#include "World/Chunks/Chunk.h"
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

float ValleyRiverStrengthAt(int x, int z, uint32_t seed,
                            const ValleyParams &params, float river_width)
{
  const ClimateSample climate = SampleClimate(x, z, seed);
  const float profile_width = river_width * params.widthSigma / 2.5f;
  return SampleRiverChannelStrength(x, z, seed, params.riverNoiseScale,
                                    profile_width, climate.erosion,
                                    climate.weirdness);
}

float SmoothedValleyRiverStrengthAt(int x, int z, uint32_t seed,
                                    const ValleyParams &params,
                                    float river_width)
{
  float sum = 0.0f;
  float weight_sum = 0.0f;
  for (int dz = -2; dz <= 2; ++dz)
  {
    for (int dx = -2; dx <= 2; ++dx)
    {
      const float weight = static_cast<float>((3 - std::abs(dx)) *
                                              (3 - std::abs(dz)));
      sum += ValleyRiverStrengthAt(x + dx, z + dz, seed, params, river_width) *
             weight;
      weight_sum += weight;
    }
  }
  return sum / weight_sum;
}

void MaterializeValleyColumn(WorldGenContext &ctx, int wx, int wz,
                             int column_surface_y, int column_depth, int sea_level)
{
  if (column_depth <= 0)
  {
    return;
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
    if (y < ctx.Settings.MinHeight)
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
  ctx.AccumulateDirtyColumn(std::max(ctx.Settings.MinHeight, carved_bottom),
                            column_surface_y);
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

  const float river_strength =
      SmoothedValleyRiverStrengthAt(x, z, seed, params, river_width);
  if (river_strength <= 0.05f)
  {
    return;
  }
  const int max_depth =
      std::max(1, static_cast<int>(static_cast<float>(params.maxDepth) *
                                   river_strength));
  const int column_surface_y = LocalSurfaceY(x, z, surface_y, get_surface_y);
  int column_depth = max_depth;
  if (column_surface_y <= sea_level + 2)
  {
    column_depth = static_cast<int>(
        static_cast<float>(column_depth) * params.aquaticDepthScale);
    column_depth = std::max(1, column_depth);
  }
  MaterializeValleyColumn(ctx, x, z, column_surface_y, column_depth, sea_level);
}

void CarveChunkValleys(WorldGenContext &ctx, int base_x, int base_z,
                       uint32_t seed, const ValleyParams &params, int sea_level,
                       float river_width,
                       const ValleySurfaceYCallback &get_surface_y)
{
  if (!params.enabled || params.maxDepth <= 0)
  {
    return;
  }

  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int wx = base_x + lx;
      const int wz = base_z + lz;
      const float river_strength =
          SmoothedValleyRiverStrengthAt(wx, wz, seed, params, river_width);
      if (river_strength <= 0.05f)
      {
        continue;
      }
      const int column_surface_y =
          LocalSurfaceY(wx, wz, sea_level, get_surface_y);
      int column_depth =
          std::max(1, static_cast<int>(static_cast<float>(params.maxDepth) *
                                       river_strength));
      if (column_surface_y <= sea_level + 2)
      {
        column_depth = static_cast<int>(
            static_cast<float>(column_depth) * params.aquaticDepthScale);
        column_depth = std::max(1, column_depth);
      }
      MaterializeValleyColumn(ctx, wx, wz, column_surface_y, column_depth,
                              sea_level);
    }
  }
}

} // namespace cutum
