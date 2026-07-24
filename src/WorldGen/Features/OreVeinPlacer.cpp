#include "WorldGen/Features/OreVeinPlacer.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
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

BlockId ResolveOreBlockId(const WorldGenContext &ctx, const std::string &slot)
{
  if (slot == "ore_coal")
  {
    return ctx.Blocks.OreCoal;
  }
  if (slot == "ore_iron")
  {
    return ctx.Blocks.OreIron;
  }

  const WorldGenSlotSpec *spec = UWorldGenRefs::GetSlot(slot);
  if (!spec)
  {
    return BLOCK_AIR;
  }
  for (const std::string &block_name : spec->BlockNames)
  {
    const BlockId id = ResolvePackScatterBlockId(
        ctx.Registry, ctx.WorldgenOwnerPackId, block_name);
    if (id != BLOCK_AIR)
    {
      return id;
    }
  }
  return BLOCK_AIR;
}

std::vector<PackOreRule> DefaultOreRules()
{
  PackOreRule coal;
  coal.Slot = "ore_coal";
  coal.YPeak = 42;
  coal.YSpread = 36;
  coal.Rarity = 0.12f;
  coal.MaxSurfaceOffset = 5;

  PackOreRule iron;
  iron.Slot = "ore_iron";
  iron.YPeak = 24;
  iron.YSpread = 28;
  iron.Rarity = 0.08f;
  iron.BelowSeaLevel = true;
  iron.SeedModulo = 3;

  return {coal, iron};
}

bool TryPlaceOreRule(const WorldGenContext &ctx, const PackOreRule &rule,
                     int x, int y, int z, int surface_y, uint32_t seed,
                     float ore_density, float noise01)
{
  const BlockId ore_id = ResolveOreBlockId(ctx, rule.Slot);
  if (ore_id == BLOCK_AIR)
  {
    return false;
  }
  if (y >= surface_y - rule.MaxSurfaceOffset)
  {
    return false;
  }
  if (rule.BelowSeaLevel && y >= ctx.Settings.SeaLevel)
  {
    return false;
  }
  if (rule.SeedModulo > 0 && (y + static_cast<int>(seed)) % rule.SeedModulo != 0)
  {
    return false;
  }

  const int y_min = std::max(1, rule.YPeak - rule.YSpread);
  const int y_max = rule.YPeak + rule.YSpread;
  const float y_factor = TriangularYFactor(y, y_min, rule.YPeak, y_max);
  const float threshold = 1.0f - rule.Rarity * ore_density;
  return noise01 * y_factor > threshold;
}

} // namespace

void FillOreVeins(WorldGenContext &ctx, int x, int z, int surfaceY, uint32_t seed,
                  float oreDensity)
{
  if (!ctx.Settings.EnableOres || oreDensity <= 0.0f)
  {
    return;
  }

  const PackOresConfig &pack_ores = UWorldGenPack::OresConfig();
  const std::vector<PackOreRule> rules =
      pack_ores.Loaded ? pack_ores.Rules : DefaultOreRules();
  if (rules.empty())
  {
    return;
  }

  const int min_y = 1;
  const int max_y = std::max(min_y, surfaceY - 3);
  const float density = std::clamp(oreDensity, 0.0f, 2.0f);

  for (int y = min_y; y <= max_y; ++y)
  {
    const float noise = FBM3D(static_cast<float>(x) * 0.07f,
                              static_cast<float>(y) * 0.09f,
                              static_cast<float>(z) * 0.07f, seed + 6100, 3, 0.5f,
                              2.0f);
    const float noise01 = (noise + 1.0f) * 0.5f;
    const glm::ivec3 pos(x, y, z);
    if (!IsStoneLike(ctx, pos) || HasAdjacentAir(ctx, pos))
    {
      continue;
    }

    for (const PackOreRule &rule : rules)
    {
      if (!TryPlaceOreRule(ctx, rule, x, y, z, surfaceY, seed, density, noise01))
      {
        continue;
      }
      ctx.World.SetBlock(pos, ResolveOreBlockId(ctx, rule.Slot));
      break;
    }
  }
  ctx.AccumulateDirtyColumn(min_y, max_y);
}

} // namespace cutum
