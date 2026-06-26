#include "WorldGen/Features/OreVeinPlacer.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

bool IsStoneLike(WorldGenContext &ctx, const glm::ivec3 &pos)
{
  const BlockId id = ctx.World.GetBlock(pos);
  return id == ctx.Blocks.Stone || id == ctx.Blocks.Gravel || id == ctx.Blocks.Dirt;
}

bool HasAdjacentAir(const WorldGenContext &ctx, const glm::ivec3 &pos)
{
  static const glm::ivec3 kDirs[] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  for (const glm::ivec3 &d : kDirs)
  {
    if (ctx.World.IsAir(pos + d))
    {
      return true;
    }
  }
  return false;
}

} // namespace

void FillOreVeins(WorldGenContext &ctx, int x, int z, int surfaceY, uint32_t seed,
                  float oreDensity)
{
  if (!ctx.Settings.EnableOres || oreDensity <= 0.0f)
  {
    return;
  }
  if (ctx.Blocks.OreCoal == BLOCK_AIR && ctx.Blocks.OreIron == BLOCK_AIR)
  {
    return;
  }

  const int minY = 1;
  const int maxY = std::max(minY, surfaceY - 3);
  const float density = std::clamp(oreDensity, 0.0f, 2.0f);

  for (int y = minY; y <= maxY; ++y)
  {
    const float n = FBM3D(static_cast<float>(x) * 0.07f,
                          static_cast<float>(y) * 0.09f,
                          static_cast<float>(z) * 0.07f, seed + 6100, 3, 0.5f,
                          2.0f);
    const float n01 = (n + 1.0f) * 0.5f;
    const glm::ivec3 pos(x, y, z);
    if (!IsStoneLike(ctx, pos) || HasAdjacentAir(ctx, pos))
    {
      continue;
    }

    const float coalY = TriangularYFactor(y, 8, 42, 80);
    if (ctx.Blocks.OreCoal != BLOCK_AIR && y < surfaceY - 5 &&
        n01 * coalY > 1.0f - 0.12f * density)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.OreCoal);
      continue;
    }

    const float ironY = TriangularYFactor(y, 4, 24, 56);
    if (ctx.Blocks.OreIron != BLOCK_AIR && y < ctx.Settings.SeaLevel &&
        n01 * ironY > 1.0f - 0.08f * density && (y + seed) % 3 == 0)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.OreIron);
    }
  }
  ctx.AccumulateDirtyColumn(minY, maxY);
}

} // namespace cutum
