#include "WorldGen/Stages/MudflowErosion.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <algorithm>
#include <array>
#include <vector>

namespace cutum
{

namespace
{

constexpr int kChunkSize = 16;

bool IsMudflowMovable(BlockId id, const WorldGenContext &ctx)
{
  return id == ctx.Blocks.Dirt || id == ctx.Blocks.Gravel || id == ctx.Blocks.Sand ||
         id == ctx.Blocks.Grass;
}

int TopSolidY(WorldGenContext &ctx, int x, int z, int max_scan_y)
{
  return FindTopSolidSurfaceY(ctx.World, ctx.Registry, x, z, max_scan_y);
}

} // namespace

void ApplyMudflowToChunk(WorldGenContext &ctx, int base_x, int base_z,
                         int iterations)
{
  const int max_scan_y = ctx.Settings.MaxHeight;
  std::array<std::array<int, kChunkSize>, kChunkSize> heights{};
  for (int lz = 0; lz < kChunkSize; ++lz)
  {
    for (int lx = 0; lx < kChunkSize; ++lx)
    {
      heights[static_cast<size_t>(lz)][static_cast<size_t>(lx)] =
          TopSolidY(ctx, base_x + lx, base_z + lz, max_scan_y);
    }
  }

  const int clamp_iterations = std::clamp(iterations, 1, 2);
  for (int iter = 0; iter < clamp_iterations; ++iter)
  {
    for (int lz = 0; lz < kChunkSize; ++lz)
    {
      for (int lx = 0; lx < kChunkSize; ++lx)
      {
        const int x = base_x + lx;
        const int z = base_z + lz;
        int &height = heights[static_cast<size_t>(lz)][static_cast<size_t>(lx)];
        int best_neighbor_lx = -1;
        int best_neighbor_lz = -1;
        int best_neighbor_h = height;
        for (int dz = -1; dz <= 1; ++dz)
        {
          for (int dx = -1; dx <= 1; ++dx)
          {
            if (dx == 0 && dz == 0)
            {
              continue;
            }
            const int nlx = lx + dx;
            const int nlz = lz + dz;
            if (nlx < 0 || nlx >= kChunkSize || nlz < 0 || nlz >= kChunkSize)
            {
              continue;
            }
            const int neighbor_h =
                heights[static_cast<size_t>(nlz)][static_cast<size_t>(nlx)];
            if (height - neighbor_h >= 2 && neighbor_h < best_neighbor_h)
            {
              best_neighbor_h = neighbor_h;
              best_neighbor_lx = nlx;
              best_neighbor_lz = nlz;
            }
          }
        }
        if (best_neighbor_lx < 0)
        {
          continue;
        }

        const glm::ivec3 from(x, height, z);
        const BlockId surface_id = ctx.World.GetBlock(from);
        if (!IsMudflowMovable(surface_id, ctx))
        {
          continue;
        }

        const int target_x = base_x + best_neighbor_lx;
        const int target_z = base_z + best_neighbor_lz;
        const int target_y = best_neighbor_h + 1;
        const glm::ivec3 to(target_x, target_y, target_z);
        if (ctx.World.IsAir(to))
        {
          ctx.World.SetBlock(to, surface_id);
          ctx.World.SetBlock(from, BLOCK_AIR);
          height = std::max(1, height - 1);
          heights[static_cast<size_t>(best_neighbor_lz)]
                 [static_cast<size_t>(best_neighbor_lx)] = target_y;
          ctx.AccumulateDirtyColumn(std::min(height, target_y),
                                    std::max(height, target_y));
        }
      }
    }
  }
}

} // namespace cutum
