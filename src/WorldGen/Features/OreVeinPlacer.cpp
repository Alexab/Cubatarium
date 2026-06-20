#include "WorldGen/Features/OreVeinPlacer.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

bool IsStoneLike(WorldGenContext &ctx, const glm::ivec3 &pos)
{
  const BlockId id = ctx.World.GetBlock(pos);
  return id == ctx.Stone || id == ctx.Gravel || id == ctx.Dirt;
}

} // namespace

void FillOreVeins(WorldGenContext &ctx, int x, int z, int surfaceY, uint32_t seed,
                  float oreDensity)
{
  if (!ctx.Settings.EnableOres || oreDensity <= 0.0f)
  {
    return;
  }
  if (ctx.OreCoal == BLOCK_AIR && ctx.OreIron == BLOCK_AIR)
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
                          static_cast<float>(z) * 0.07f, seed + 6100, 2, 0.5f,
                          2.0f);
    const float n01 = (n + 1.0f) * 0.5f;
    const glm::ivec3 pos(x, y, z);
    if (!IsStoneLike(ctx, pos))
    {
      continue;
    }

    if (ctx.OreCoal != BLOCK_AIR && y < surfaceY - 5 &&
        n01 > 1.0f - 0.08f * density)
    {
      ctx.World.SetBlock(pos, ctx.OreCoal);
      continue;
    }
    if (ctx.OreIron != BLOCK_AIR && y < ctx.Settings.SeaLevel &&
        n01 > 1.0f - 0.05f * density && (y + seed) % 3 == 0)
    {
      ctx.World.SetBlock(pos, ctx.OreIron);
    }
  }
  ctx.MarkDirtyColumn(x, z, minY, maxY);
}

} // namespace cutum
