#include "WorldGen/Features/RavineCarver.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

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

float RavineWidthProfile(const RavineParams &params, float dist)
{
  const float t = std::clamp(dist / 3.5f, 0.0f, 1.0f);
  if (params.featherMode == RavineFeatherMode::Linear)
  {
    return 1.0f - t;
  }
  return 1.0f - Smoothstep(0.0f, 1.0f, t);
}

std::optional<RavineParams> RavineTriggerParams(int x, int z, uint32_t seed,
                                                const RavineParams &params)
{
  if (!params.enabled || params.rarity <= 0)
  {
    return std::nullopt;
  }
  if (RavineHash(x, z, seed) % static_cast<uint32_t>(params.rarity) != 0)
  {
    return std::nullopt;
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
    return std::nullopt;
  }

  const float depth_factor = Smoothstep(0.82f, 0.95f, combined);
  RavineParams effective = params;
  effective.maxDepth =
      params.minDepth +
      static_cast<int>(depth_factor *
                       static_cast<float>(params.maxDepth - params.minDepth));
  effective.minDepth = effective.maxDepth;
  return effective;
}

void MaterializeRavineColumn(WorldGenContext &ctx, int wx, int wz,
                             int column_surface_y, int column_depth,
                             int sea_level, const RavineParams &params)
{
  if (column_depth <= 0)
  {
    return;
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
  if (params.fillWater && ctx.Blocks.Water != BLOCK_AIR &&
      carved_bottom < column_surface_y)
  {
    const int fill_top = std::min(column_surface_y, sea_level);
    for (int y = carved_bottom; y <= fill_top; ++y)
    {
      const glm::ivec3 pos(wx, y, wz);
      if (ctx.World.IsAir(pos))
      {
        ctx.World.SetBlock(pos, ctx.Blocks.Water);
      }
    }
  }
  ctx.AccumulateDirtyColumn(std::max(1, carved_bottom), column_surface_y);
}

void AccumulateRavineCarveDepth(
    int base_x, int base_z, int trigger_x, int trigger_z,
    const RavineParams &effective,
    std::array<std::array<int, CHUNK_SIZE>, CHUNK_SIZE> &carve_depth)
{
  for (int dz = -3; dz <= 3; ++dz)
  {
    for (int dx = -3; dx <= 3; ++dx)
    {
      const int wx = trigger_x + dx;
      const int wz = trigger_z + dz;
      if (wx < base_x || wx >= base_x + CHUNK_SIZE || wz < base_z ||
          wz >= base_z + CHUNK_SIZE)
      {
        continue;
      }
      const int lx = wx - base_x;
      const int lz = wz - base_z;
      const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      const float width_profile = RavineWidthProfile(effective, dist);
      int column_depth = static_cast<int>(
          static_cast<float>(effective.maxDepth) * width_profile);
      if (column_depth <= 0)
      {
        continue;
      }

      int &slot = carve_depth[static_cast<size_t>(lz)][static_cast<size_t>(lx)];
      slot = std::max(slot, column_depth);
    }
  }
}

void MaterializeRavineChunkCarves(
    WorldGenContext &ctx, int base_x, int base_z,
    const std::array<std::array<int, CHUNK_SIZE>, CHUNK_SIZE> &surface_y,
    const std::array<std::array<int, CHUNK_SIZE>, CHUNK_SIZE> &carve_depth,
    int sea_level, const RavineParams &params,
    const RavineSurfaceYCallback &get_surface_y)
{
  (void)get_surface_y;
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
      if (column_surface_y <= sea_level + 2 && params.aquaticMaxDepth > 0)
      {
        column_depth =
            std::min(column_depth, std::max(1, params.aquaticMaxDepth));
      }
      MaterializeRavineColumn(ctx, wx, wz, column_surface_y, column_depth,
                              sea_level, params);
    }
  }
}

} // namespace

void CarveColumnRavinesDeterministic(WorldGenContext &ctx, int x, int z,
                                     int surface_y, const RavineParams &params,
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
      const float width_profile = RavineWidthProfile(params, dist);
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

      MaterializeRavineColumn(ctx, wx, wz, column_surface_y, column_depth,
                              sea_level, params);
    }
  }
}

void CarveColumnRavines(WorldGenContext &ctx, int x, int z, int surface_y,
                        uint32_t seed, const RavineParams &params, int sea_level,
                        const RavineSurfaceYCallback &get_surface_y)
{
  const std::optional<RavineParams> effective =
      RavineTriggerParams(x, z, seed, params);
  if (!effective)
  {
    return;
  }
  CarveColumnRavinesDeterministic(ctx, x, z, surface_y, *effective, sea_level,
                                  get_surface_y);
}

void CarveChunkRavines(WorldGenContext &ctx, int base_x, int base_z,
                       uint32_t seed, const RavineParams &params, int sea_level,
                       const RavineSurfaceYCallback &get_surface_y)
{
  if (!params.enabled || params.rarity <= 0)
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
          ResolveColumnSurfaceY(wx, wz, sea_level, get_surface_y);
    }
  }

  constexpr int kHalo = 3;
  for (int tz = base_z - kHalo; tz < base_z + CHUNK_SIZE + kHalo; ++tz)
  {
    for (int tx = base_x - kHalo; tx < base_x + CHUNK_SIZE + kHalo; ++tx)
    {
      const std::optional<RavineParams> effective =
          RavineTriggerParams(tx, tz, seed, params);
      if (!effective)
      {
        continue;
      }

      AccumulateRavineCarveDepth(base_x, base_z, tx, tz, *effective, carve_depth);
    }
  }

  MaterializeRavineChunkCarves(ctx, base_x, base_z, surface_y, carve_depth,
                               sea_level, params, get_surface_y);
}

void CarveChunkRavinesDeterministic(WorldGenContext &ctx, int base_x, int base_z,
                                    int trigger_x, int trigger_z,
                                    const RavineParams &params, int sea_level,
                                    const RavineSurfaceYCallback &get_surface_y)
{
  if (!params.enabled)
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
          ResolveColumnSurfaceY(wx, wz, sea_level, get_surface_y);
    }
  }

  AccumulateRavineCarveDepth(base_x, base_z, trigger_x, trigger_z, params,
                             carve_depth);
  MaterializeRavineChunkCarves(ctx, base_x, base_z, surface_y, carve_depth,
                               sea_level, params, get_surface_y);
}

} // namespace cutum
