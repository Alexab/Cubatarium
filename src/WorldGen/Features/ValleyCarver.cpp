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
#include <limits>
#include <vector>

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
                                    climate.weirdness, params.warpBlocks);
}

// 1 on flat lowland, 0 on hillsides and highlands (profile 3).
float ValleyLowlandFade(int wx, int wz, int surface_y, int sea_level,
                        const ValleySurfaceYCallback &get_surface_y)
{
  const float above_sea = static_cast<float>(surface_y - sea_level);
  float fade = 1.0f - Smoothstep(6.0f, 30.0f, above_sea);
  if (fade > 0.0f && get_surface_y)
  {
    int max_delta = 0;
    constexpr int kOffsets[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
    for (const auto &offset : kOffsets)
    {
      max_delta = std::max(
          max_delta,
          std::abs(get_surface_y(wx + offset[0], wz + offset[1]) - surface_y));
    }
    fade *= 1.0f - Smoothstep(2.0f, 7.0f, static_cast<float>(max_delta));
  }
  return fade;
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
                             int column_surface_y, int column_depth,
                             const ValleyParams &params, float river_strength,
                             float moisture, int sea_level)
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

  if (!params.fillWater || !ctx.Settings.FillWater ||
      ctx.Blocks.Water == BLOCK_AIR ||
      river_strength < params.waterMinStrength ||
      moisture < params.waterMinMoisture)
  {
    return;
  }

  const int water_surface_y =
      column_surface_y - std::max(0, params.waterSurfaceDepth);
  const int maximum_water_y =
      sea_level + std::max(0, params.maxWaterElevationAboveSea);
  if (water_surface_y > maximum_water_y)
  {
    return;
  }
  const int lower_bound = std::max(ctx.Settings.MinHeight,
                                   column_surface_y - column_depth + 1);
  if (water_surface_y < lower_bound)
  {
    return;
  }

  int water_bottom_y = water_surface_y;
  for (int y = water_surface_y; y >= lower_bound; --y)
  {
    const glm::ivec3 pos(wx, y, wz);
    if (!ctx.World.IsAir(pos))
    {
      break;
    }
    ctx.World.SetBlock(pos, ctx.Blocks.Water);
    ctx.World.SetFluidState(pos,
                            FluidCellState::Source().WithKind(FluidKind::Water));
    water_bottom_y = y;
  }
  if (water_bottom_y < water_surface_y)
  {
    ctx.AccumulateDirtyColumn(water_bottom_y, water_surface_y);
  }
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
  const float moisture = SampleClimate(x, z, seed).moisture;
  int column_depth = max_depth;
  if (params.lowlandOnly)
  {
    column_depth = static_cast<int>(
        static_cast<float>(column_depth) *
        ValleyLowlandFade(x, z, column_surface_y, sea_level, get_surface_y));
    if (column_depth <= 0)
    {
      return;
    }
  }
  if (column_surface_y <= sea_level + 2)
  {
    column_depth = static_cast<int>(
        static_cast<float>(column_depth) * params.aquaticDepthScale);
    column_depth = std::max(1, column_depth);
  }
  MaterializeValleyColumn(ctx, x, z, column_surface_y, column_depth, params,
                          river_strength, moisture, sea_level);
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

  // Terrain height is the expensive part of the lowland fade (five lookups per
  // river column, most shared with neighbours). Memoise it over the chunk plus
  // the 3-block fade reach so each height is evaluated once.
  constexpr int kReach = 3;
  constexpr int kSpan = CHUNK_SIZE + 2 * kReach;
  std::vector<int> surface_cache(static_cast<size_t>(kSpan) * kSpan,
                                 std::numeric_limits<int>::min());
  const ValleySurfaceYCallback cached_surface_y =
      get_surface_y
          ? ValleySurfaceYCallback(
                [&](int wx, int wz) -> int
                {
                  const int ix = wx - (base_x - kReach);
                  const int iz = wz - (base_z - kReach);
                  if (ix < 0 || iz < 0 || ix >= kSpan || iz >= kSpan)
                  {
                    return get_surface_y(wx, wz);
                  }
                  int &slot = surface_cache[static_cast<size_t>(iz) * kSpan + ix];
                  if (slot == std::numeric_limits<int>::min())
                  {
                    slot = get_surface_y(wx, wz);
                  }
                  return slot;
                })
          : ValleySurfaceYCallback();

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
          LocalSurfaceY(wx, wz, sea_level, cached_surface_y);
      const float moisture = SampleClimate(wx, wz, seed).moisture;
      int column_depth =
          std::max(1, static_cast<int>(static_cast<float>(params.maxDepth) *
                                       river_strength));
      if (params.lowlandOnly)
      {
        column_depth = static_cast<int>(
            static_cast<float>(column_depth) *
            ValleyLowlandFade(wx, wz, column_surface_y, sea_level,
                              cached_surface_y));
        if (column_depth <= 0)
        {
          continue;
        }
      }
      if (column_surface_y <= sea_level + 2)
      {
        column_depth = static_cast<int>(
            static_cast<float>(column_depth) * params.aquaticDepthScale);
        column_depth = std::max(1, column_depth);
      }
      MaterializeValleyColumn(ctx, wx, wz, column_surface_y, column_depth,
                              params, river_strength, moisture, sea_level);
    }
  }
}

} // namespace cutum
