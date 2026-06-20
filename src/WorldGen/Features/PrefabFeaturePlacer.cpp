#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Prefabs/Prefab.h"
#include "World/Prefabs/PrefabUtil.h"
#include <climits>
#include <cstdint>

namespace cutum
{

namespace
{

uint32_t FeatureHash(int x, int z, uint32_t Seed)
{
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ Seed;
}

bool BiomeMatches(BiomeId biome, const std::vector<BiomeId> &allowed)
{
  for (BiomeId allowedBiome : allowed)
  {
    if (allowedBiome == biome)
    {
      return true;
    }
  }
  return false;
}

int EffectiveSpacing(int spacing, float density)
{
  if (density <= 0.0f)
  {
    return INT_MAX;
  }
  if (spacing <= 0)
  {
    return spacing;
  }
  return std::max(1, static_cast<int>(static_cast<float>(spacing) / density));
}

int EffectiveChance(int chancePerColumn, float density)
{
  if (density <= 0.0f)
  {
    return INT_MAX;
  }
  if (chancePerColumn <= 0)
  {
    return chancePerColumn;
  }
  return std::max(1, static_cast<int>(static_cast<float>(chancePerColumn) / density));
}

int ResolvePlacementYOffset(const WorldGenContext &ctx,
                            const PrefabFeatureRule &rule)
{
  if (rule.PlacementYOffset != 0)
  {
    return rule.PlacementYOffset;
  }
  if (!ctx.Prefabs)
  {
    return 0;
  }
  const Prefab *prefab = ctx.Prefabs->Get(rule.PrefabName);
  if (!prefab)
  {
    return 0;
  }
  return prefab->PlacementYOffset;
}

bool PrefabRequiresNearWater(const std::string &prefabName)
{
  return prefabName.rfind("reeds_", 0) == 0;
}

bool IsNearSurfaceWater(const WorldGenContext &ctx, int x, int z, int surfaceY,
                        int radius = 5)
{
  if (ctx.Water == BLOCK_AIR)
  {
    return false;
  }
  for (int dz = -radius; dz <= radius; ++dz)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      for (int dy = -1; dy <= 1; ++dy)
      {
        if (ctx.World.GetBlock(glm::ivec3(x + dx, surfaceY + dy, z + dz)) ==
            ctx.Water)
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool TryPlacePrefabPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                        BiomeId biome,
                        const std::vector<PrefabFeatureRule> &rules,
                        uint32_t pickSeedOffset, bool useChanceOnly,
                        bool requireTreesEnabled, float densityMultiplier)
{
  if ((requireTreesEnabled && !ctx.Settings.EnableTrees) || !ctx.Prefabs ||
      rules.empty())
  {
    return false;
  }

  struct Candidate
  {
    const PrefabFeatureRule *Rule;
    int Weight;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(rules.size());
  const uint32_t seed = ctx.Settings.Seed;

  for (const PrefabFeatureRule &rule : rules)
  {
    if (!BiomeMatches(biome, rule.Biomes))
    {
      continue;
    }
    if (!ctx.Prefabs->Get(rule.PrefabName))
    {
      continue;
    }
    const uint32_t h = FeatureHash(x, z, seed + rule.SeedOffset);
    if (useChanceOnly)
    {
      const int chance = EffectiveChance(rule.ChancePerColumn, densityMultiplier);
      if (chance <= 0 || h % static_cast<uint32_t>(chance) != 0)
      {
        continue;
      }
    }
    else
    {
      const int spacing = EffectiveSpacing(rule.Spacing, densityMultiplier);
      if (spacing <= 0 || h % static_cast<uint32_t>(spacing) != 0)
      {
        continue;
      }
    }
    candidates.push_back({&rule, std::max(1, rule.Weight)});
  }

  if (candidates.empty())
  {
    return false;
  }

  int totalWeight = 0;
  for (const Candidate &c : candidates)
  {
    totalWeight += c.Weight;
  }
  int pick = static_cast<int>(
      FeatureHash(x, z, seed + pickSeedOffset) % static_cast<uint32_t>(totalWeight));
  const PrefabFeatureRule *chosen = candidates.front().Rule;
  for (const Candidate &c : candidates)
  {
    pick -= c.Weight;
    if (pick < 0)
    {
      chosen = c.Rule;
      break;
    }
  }

  const int yOffset = ResolvePlacementYOffset(ctx, *chosen);
  const glm::ivec3 anchor(x, surfaceY + 1 + yOffset, z);
  if (PrefabRequiresNearWater(chosen->PrefabName) &&
      !IsNearSurfaceWater(ctx, x, z, surfaceY))
  {
    return false;
  }
  return PlacePrefabAt(ctx, chosen->PrefabName, anchor, surfaceY);
}

} // namespace

bool CanPlacePrefabAt(const WorldGenContext &ctx, const std::string &prefabName,
                      glm::ivec3 anchorWorldPos)
{
  if (!ctx.Prefabs)
  {
    return false;
  }
  const Prefab *prefab = ctx.Prefabs->Get(prefabName);
  if (!prefab)
  {
    return false;
  }
  return CanPlacePrefabAt(ctx.World, *prefab, anchorWorldPos);
}

bool PlacePrefabAt(WorldGenContext &ctx, const std::string &prefabName,
                   glm::ivec3 anchorWorldPos, int surfaceY)
{
  if (!ctx.Prefabs)
  {
    return false;
  }
  const Prefab *prefab = ctx.Prefabs->Get(prefabName);
  if (!prefab)
  {
    return false;
  }
  const bool canPlace =
      surfaceY >= 0
          ? CanPlacePrefabAtForWorldGen(ctx.World, *prefab, anchorWorldPos,
                                        surfaceY)
          : CanPlacePrefabAt(ctx.World, *prefab, anchorWorldPos);
  if (!canPlace)
  {
    return false;
  }
  const PrefabPlacementStats stats =
      PlacePrefabAt(ctx.World, *prefab, anchorWorldPos, false);
  if (stats.placedCount == 0)
  {
    return false;
  }
  ctx.MarkDirtyColumn(anchorWorldPos.x, anchorWorldPos.z, stats.minY,
                      stats.maxY);
  return true;
}

bool TryPlaceTree(WorldGenContext &ctx, int x, int z, int surfaceY,
                  BiomeId biome, const FeatureParams &params)
{
  if (UPrefabFeatureConfigStorage::IsLoaded())
  {
    return TryPlaceVegetationFeatures(ctx, x, z, surfaceY, biome);
  }

  if (!ctx.Settings.EnableTrees || !ctx.Prefabs)
  {
    return false;
  }
  if (biome != BiomeId::Forest && biome != BiomeId::Plains)
  {
    return false;
  }

  const glm::ivec3 anchor(x, surfaceY + 1, z);
  const uint32_t Seed = ctx.Settings.Seed;

  if (biome == BiomeId::Forest)
  {
    if (FeatureHash(x, z, Seed + params.treeLargeSeedOffset) %
            static_cast<uint32_t>(params.treeLargeSpacingModForest) ==
        0)
    {
      return PlacePrefabAt(ctx, params.treeLargePrefabName, anchor, surfaceY);
    }
    if (FeatureHash(x, z, Seed + params.treeSeedOffset) %
            static_cast<uint32_t>(params.treeSmallSpacingModForest) ==
        0)
    {
      return PlacePrefabAt(ctx, params.treeSmallPrefabName, anchor, surfaceY);
    }
    return false;
  }

  if (FeatureHash(x, z, Seed + params.treeSeedOffset) %
          static_cast<uint32_t>(params.treeSmallSpacingModPlains) !=
      0)
  {
    return false;
  }
  if (FeatureHash(x, z, Seed + params.treeSeedOffset + 7) % 5 != 0)
  {
    return false;
  }
  return PlacePrefabAt(ctx, params.treeSmallPrefabName, anchor, surfaceY);
}

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded())
  {
    return false;
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().Vegetation, 5000,
                            false, true, ctx.Settings.Tuning.vegetationDensity);
}

bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded())
  {
    return false;
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().Decoration, 6000,
                            false, false, ctx.Settings.Tuning.decorationDensity);
}

bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded())
  {
    return false;
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().Structures, 7000,
                            true, false, ctx.Settings.Tuning.structureDensity);
}

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome)
{
  if (!ctx.Settings.FillLava || ctx.Lava == BLOCK_AIR ||
      biome != BiomeId::Hills)
  {
    return false;
  }
  const uint32_t Seed = ctx.Settings.Seed;
  if (FeatureHash(x, z, Seed + 9001) % 400 != 0)
  {
    return false;
  }
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      const glm::ivec3 pos(x + dx, surfaceY + 1, z + dz);
      const BlockId below =
          ctx.World.GetBlock(glm::ivec3(x + dx, surfaceY, z + dz));
      if (below != ctx.Stone && below != ctx.Gravel)
      {
        return false;
      }
      if (!ctx.World.IsAir(pos))
      {
        return false;
      }
      ctx.World.SetBlock(pos, ctx.Lava);
    }
  }
  ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
  return true;
}

bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId)
{
  if (!ctx.Settings.FillFire || ctx.Fire == BLOCK_AIR)
  {
    return false;
  }
  (void)biome;
  const BlockId surface = ctx.World.GetBlock(glm::ivec3(x, surfaceY, z));
  if (surface != grassId)
  {
    return false;
  }
  const glm::ivec3 firePos(x, surfaceY + 1, z);
  if (!ctx.World.IsAir(firePos))
  {
    return false;
  }
  const uint32_t Seed = ctx.Settings.Seed;
  if (FeatureHash(x, z, Seed + 12007) % 512 != 0)
  {
    return false;
  }
  ctx.World.SetBlock(firePos, ctx.Fire);
  ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
  return true;
}

} // namespace cutum
