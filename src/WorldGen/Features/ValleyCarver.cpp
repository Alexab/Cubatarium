#include "WorldGen/Features/ValleyCarver.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include <algorithm>
#include <array>
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
  const float pv = PeaksAndValleys(climate.weirdness);
  const float valley_boost = pv < 0.35f ? 1.25f : 1.0f;
  const float river_noise =
      NormalizedFBM2D(static_cast<float>(x) * params.riverNoiseScale,
                      static_cast<float>(z) * params.riverNoiseScale,
                      seed + 8801, 2, 0.5f, 2.0f);
  const float width =
      river_width * (1.0f - climate.erosion * 0.5f) * valley_boost;
  return Smoothstep(0.50f, 0.72f, (river_noise + 1.0f) * 0.5f) * width;
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

} // namespace

void CarveColumnValleys(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const ValleyParams &params, int sea_level,
                        float river_width, const ValleySurfaceYCallback &get_surface_y)
{
  if (!params.enabled || params.maxDepth <= 0)
  {
    return;
  }

  const float river_strength = ValleyRiverStrengthAt(x, z, seed, params, river_width);
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

      MaterializeValleyColumn(ctx, wx, wz, column_surface_y, column_depth,
                              sea_level);
    }
  }
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

  std::array<std::array<int, CHUNK_SIZE>, CHUNK_SIZE> surface_y{};
  std::array<std::array<int, CHUNK_SIZE>, CHUNK_SIZE> carve_depth{};

  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int wx = base_x + lx;
      const int wz = base_z + lz;
      surface_y[static_cast<size_t>(lz)][static_cast<size_t>(lx)] =
          LocalSurfaceY(wx, wz, sea_level, get_surface_y);
    }
  }

  const float sigma = std::max(0.75f, params.widthSigma);
  const float sigma_sq = sigma * sigma * 2.0f;

  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int x = base_x + lx;
      const int z = base_z + lz;
      const float river_strength =
          ValleyRiverStrengthAt(x, z, seed, params, river_width);
      if (river_strength <= 0.05f)
      {
        continue;
      }

      int max_depth =
          std::max(1, static_cast<int>(static_cast<float>(params.maxDepth) *
                                       river_strength));

      for (int dz = -3; dz <= 3; ++dz)
      {
        for (int dx = -3; dx <= 3; ++dx)
        {
          const int tlx = lx + dx;
          const int tlz = lz + dz;
          if (tlx < 0 || tlx >= CHUNK_SIZE || tlz < 0 || tlz >= CHUNK_SIZE)
          {
            continue;
          }
          const float dist_sq = static_cast<float>(dx * dx + dz * dz);
          const float radial = std::exp(-dist_sq / sigma_sq);
          const int column_depth =
              static_cast<int>(static_cast<float>(max_depth) * radial);
          if (column_depth <= 0)
          {
            continue;
          }
          int &slot =
              carve_depth[static_cast<size_t>(tlz)][static_cast<size_t>(tlx)];
          slot = std::max(slot, column_depth);
        }
      }
    }
  }

  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      int column_depth =
          carve_depth[static_cast<size_t>(lz)][static_cast<size_t>(lx)];
      if (column_depth <= 0)
      {
        continue;
      }
      const int wx = base_x + lx;
      const int wz = base_z + lz;
      const int column_surface_y =
          surface_y[static_cast<size_t>(lz)][static_cast<size_t>(lx)];
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
