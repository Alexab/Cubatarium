#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Prefabs/Prefab.h"
#include "World/Prefabs/PrefabUtil.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "WorldGen/Core/WorldGenPack.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>

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
  const float multiplier = std::max(0.25f, 2.0f - density);
  return std::max(1, static_cast<int>(static_cast<float>(spacing) * multiplier));
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
  const float multiplier = std::max(0.25f, 2.0f - density);
  return std::max(1, static_cast<int>(static_cast<float>(chancePerColumn) * multiplier));
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

bool PrefabRequiresGrassSurface(const std::string &prefabName)
{
  return prefabName == "campfire_ring" ||
         prefabName.rfind("deco_log_", 0) == 0;
}

bool IsGrassLandSurface(const WorldGenContext &ctx, int x, int z, int surfaceY)
{
  if (ctx.Grass == BLOCK_AIR)
  {
    return false;
  }
  return ctx.World.GetBlock(glm::ivec3(x, surfaceY, z)) == ctx.Grass &&
         ctx.World.IsAir(glm::ivec3(x, surfaceY + 1, z));
}

bool PrefabRequiresWaterSurface(const std::string &prefabName)
{
  return prefabName.rfind("lily_pad", 0) == 0;
}

bool TryWaterSurfaceAnchorAt(const WorldGenContext &ctx, int x, int z, int &anchorY)
{
  if (ctx.Water == BLOCK_AIR)
  {
    return false;
  }
  const int maxY =
      std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 1);
  for (int y = maxY; y >= 1; --y)
  {
    if (ctx.World.GetBlock(glm::ivec3(x, y, z)) != ctx.Water)
    {
      continue;
    }
    if (!ctx.World.IsAir(glm::ivec3(x, y + 1, z)))
    {
      continue;
    }
    anchorY = y + 1;
    return true;
  }
  return false;
}

bool FindWaterSurfaceAnchor(const WorldGenContext &ctx, int x, int z,
                            glm::ivec3 &anchorOut)
{
  for (int radius = 0; radius <= 3; ++radius)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      for (int dx = -radius; dx <= radius; ++dx)
      {
        if (radius > 0 && std::max(std::abs(dx), std::abs(dz)) != radius)
        {
          continue;
        }
        int anchorY = 0;
        if (TryWaterSurfaceAnchorAt(ctx, x + dx, z + dz, anchorY))
        {
          anchorOut = glm::ivec3(x + dx, anchorY, z + dz);
          return true;
        }
      }
    }
  }
  return false;
}

bool PlacePrefabAtWaterSurface(WorldGenContext &ctx, const std::string &prefabName,
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
  if (!CanPlacePrefabAt(ctx.World, *prefab, anchorWorldPos))
  {
    return false;
  }
  const PrefabPlacementStats stats =
      PlacePrefabAt(ctx.World, *prefab, anchorWorldPos, false);
  if (stats.placedCount == 0)
  {
    return false;
  }
  ctx.AccumulateDirtyColumn(stats.minY, stats.maxY);
  return true;
}

BlockId ResolveScatterBlockId(const WorldGenContext &ctx,
                              const std::string &blockName)
{
  if (blockName.empty())
  {
    return BLOCK_AIR;
  }
  if (!ctx.WorldgenOwnerPackId.empty())
  {
    const BlockId qualified = ctx.Registry.GetIdByTypeName(
        MakeQualifiedBlockName(ctx.WorldgenOwnerPackId, blockName));
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return ctx.Registry.GetIdByTypeName(blockName);
}

bool TryPlaceScatterBlocks(WorldGenContext &ctx, int x, int z, int surfaceY,
                           const PrefabFeatureRule &rule)
{
  const BlockId blockId = ResolveScatterBlockId(ctx, rule.Scatter.BlockName);
  if (blockId == BLOCK_AIR)
  {
    return false;
  }

  const uint32_t seed = ctx.Settings.Seed;
  const int radius = std::max(0, rule.Scatter.Radius);
  const int span = radius * 2 + 1;
  int placed = 0;
  int minY = surfaceY;
  int maxY = surfaceY + 1;

  for (int attempt = 0; attempt < rule.Scatter.Attempts; ++attempt)
  {
    const uint32_t h =
        FeatureHash(x, z, seed + rule.SeedOffset + static_cast<uint32_t>(attempt * 17));
    const int ox = static_cast<int>(h % static_cast<uint32_t>(span)) - radius;
    const int oz =
        static_cast<int>((h / 31U) % static_cast<uint32_t>(span)) - radius;
    const int wx = x + ox;
    const int wz = z + oz;
    const int maxScanY =
        std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 4);
    const int localSurface =
        FindTopSolidSurfaceY(ctx.World, ctx.Registry, wx, wz, maxScanY);
    if (localSurface < 0)
    {
      continue;
    }
    const int y = localSurface + 1 + rule.Scatter.DyOffset;
    const glm::ivec3 pos(wx, y, wz);
    if (!CanPlacePlantAt(ctx.World, ctx.Registry, pos))
    {
      continue;
    }
    ctx.World.SetBlock(pos, blockId);
    ++placed;
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  }

  if (placed <= 0)
  {
    return false;
  }
  ctx.AccumulateDirtyColumn(minY, maxY);
  return true;
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

bool SubBiomeMatches(SubBiomeId subBiome,
                     const std::vector<SubBiomeId> &allowed)
{
  if (allowed.empty())
  {
    return true;
  }
  for (SubBiomeId allowedSub : allowed)
  {
    if (allowedSub == subBiome)
    {
      return true;
    }
  }
  return false;
}

float SubBiomePoolWeightMultiplier(SubBiomeId subBiome, BiomeId biome,
                                   PrefabFeaturePool pool)
{
  const std::string biomeId = BiomeIdToString(biome);
  const BiomePackDefinition *packDef =
      UWorldGenPack::BiomeDefinitionFor(biomeId);
  if (packDef &&
      packDef->SubBiomes.count(SubBiomeIdToString(subBiome)) > 0)
  {
    return UWorldGenPack::SubBiomePoolWeightMultiplier(biomeId, subBiome, pool);
  }
  if (pool == PrefabFeaturePool::Structures)
  {
    return 1.0f;
  }
  if (pool == PrefabFeaturePool::GroundCover)
  {
    return 1.0f;
  }
  if (biome == BiomeId::Forest)
  {
    switch (subBiome)
    {
    case SubBiomeId::DenseForest:
      return 1.35f;
    case SubBiomeId::SparseForest:
      return 0.75f;
    case SubBiomeId::Woodland:
      return 1.1f;
    default:
      return 1.0f;
    }
  }
  if (biome == BiomeId::Desert)
  {
    switch (subBiome)
    {
    case SubBiomeId::Dunes:
      return 0.85f;
    case SubBiomeId::ScrubDesert:
      return 1.15f;
    default:
      return 1.0f;
    }
  }
  return 1.0f;
}

bool TryPlacePrefabPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                        BiomeId biome,
                        const std::vector<PrefabFeatureRule> &rules,
                        PrefabFeaturePool pool, uint32_t pickSeedOffset,
                        bool useChanceOnly, bool requireTreesEnabled,
                        float densityMultiplier)
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
  const SubBiomeId subBiome = SubBiomeFor(x, z, biome, seed);
  const float subMul = SubBiomePoolWeightMultiplier(subBiome, biome, pool);

  for (const PrefabFeatureRule &rule : rules)
  {
    if (!BiomeMatches(biome, rule.Biomes))
    {
      continue;
    }
    if (!SubBiomeMatches(subBiome, rule.SubBiomes))
    {
      continue;
    }
    if (rule.Mode == PrefabPlacementMode::ScatterBlocks)
    {
      if (rule.Scatter.BlockName.empty() ||
          ResolveScatterBlockId(ctx, rule.Scatter.BlockName) == BLOCK_AIR)
      {
        continue;
      }
    }
    else if (!ctx.Prefabs->Get(rule.PrefabName))
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
    const std::string &featureKey =
        rule.Mode == PrefabPlacementMode::ScatterBlocks ? rule.Scatter.BlockName
                                                        : rule.PrefabName;
    const float packMul =
        UWorldGenPack::FeatureWeightMultiplier(BiomeIdToString(biome), featureKey);
    const int weight = std::max(
        1, static_cast<int>(static_cast<float>(std::max(1, rule.Weight)) *
                            subMul * packMul));
    candidates.push_back({&rule, weight});
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
  if (chosen->Mode == PrefabPlacementMode::ScatterBlocks)
  {
    if (!IsExposedLandSurface(ctx.World, ctx.Registry, x, z, surfaceY))
    {
      return false;
    }
    return TryPlaceScatterBlocks(ctx, x, z, surfaceY, *chosen);
  }
  if (PrefabRequiresWaterSurface(chosen->PrefabName))
  {
    if (!IsNearSurfaceWater(ctx, x, z, surfaceY))
    {
      return false;
    }
    glm::ivec3 waterAnchor;
    if (!FindWaterSurfaceAnchor(ctx, x, z, waterAnchor))
    {
      return false;
    }
    return PlacePrefabAtWaterSurface(ctx, chosen->PrefabName, waterAnchor);
  }
  const glm::ivec3 anchor(x, surfaceY + 1 + yOffset, z);
  if (PrefabRequiresNearWater(chosen->PrefabName) &&
      !IsNearSurfaceWater(ctx, x, z, surfaceY))
  {
    return false;
  }
  if (!IsExposedLandSurface(ctx.World, ctx.Registry, x, z, surfaceY))
  {
    return false;
  }
  if (PrefabRequiresGrassSurface(chosen->PrefabName) &&
      !IsGrassLandSurface(ctx, x, z, surfaceY))
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
  const int maxScanY =
      surfaceY >= 0
          ? std::min(ctx.Settings.MaxHeight - 1,
                     std::max(surfaceY + 24, ctx.Settings.SeaLevel + 4))
          : std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 4);
  const bool canPlace =
      surfaceY >= 0
          ? CanPlacePrefabAtForWorldGen(ctx.World, ctx.Registry, *prefab,
                                        anchorWorldPos, maxScanY)
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
  ctx.AccumulateDirtyColumn(stats.minY, stats.maxY);
  return true;
}

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded())
  {
    static bool warnedMissingFeatures = false;
    if (!warnedMissingFeatures)
    {
      std::cerr << "prefab_features missing, vegetation disabled" << std::endl;
      warnedMissingFeatures = true;
    }
    return false;
  }
  if (!ctx.Settings.EnableTrees)
  {
    return false;
  }
  if (!IsExposedLandSurface(ctx.World, ctx.Registry, x, z, surfaceY))
  {
    return false;
  }
  if (biome == BiomeId::Hills)
  {
    const int range =
        std::max(1, ctx.Settings.MaxHeight - ctx.Settings.SeaLevel);
    const float heightNorm = std::clamp(
        static_cast<float>(surfaceY - ctx.Settings.SeaLevel) /
            static_cast<float>(range),
        0.0f, 1.0f);
    if (heightNorm > 0.82f)
    {
      return false;
    }
  }
  const int minTreeSpacing = EffectiveSpacing(24, ctx.Settings.Tuning.vegetationDensity);
  constexpr int kDx[] = {-1, 1, 0, 0};
  constexpr int kDz[] = {0, 0, -1, 1};
  for (int i = 0; i < 4; ++i)
  {
    const uint32_t neighborHash =
        FeatureHash(x + kDx[i], z + kDz[i], ctx.Settings.Seed + 5000);
    if (neighborHash % static_cast<uint32_t>(std::max(1, minTreeSpacing)) == 0u)
    {
      return false;
    }
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().Vegetation,
                            PrefabFeaturePool::Vegetation, 5000, false, true,
                            ctx.Settings.Tuning.vegetationDensity);
}

bool TryPlaceGroundCoverFeatures(WorldGenContext &ctx, int x, int z,
                                 int surfaceY, BiomeId biome,
                                 bool skipIfTreeNearby)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded() || !ctx.Settings.EnableGroundCover)
  {
    return false;
  }
  if (skipIfTreeNearby)
  {
    return false;
  }
  if (!IsExposedLandSurface(ctx.World, ctx.Registry, x, z, surfaceY))
  {
    return false;
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().GroundCover,
                            PrefabFeaturePool::GroundCover, 5500, false, true,
                            ctx.Settings.Tuning.vegetationDensity * 0.85f);
}

bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableDecoration)
  {
    return false;
  }
  if (!IsExposedLandSurface(ctx.World, ctx.Registry, x, z, surfaceY))
  {
    return false;
  }
  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome,
                            UPrefabFeatureConfigStorage::Get().Decoration,
                            PrefabFeaturePool::Decoration, 6000, false, false,
                            ctx.Settings.Tuning.decorationDensity);
}

bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome)
{
  if (!UPrefabFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableStructures)
  {
    return false;
  }

  const PrefabFeatureConfig &cfg = UPrefabFeatureConfigStorage::Get();
  const int cellSize = std::max(16, cfg.structureCellSize);
  const int cellX = (x >= 0 ? x : x - cellSize + 1) / cellSize;
  const int cellZ = (z >= 0 ? z : z - cellSize + 1) / cellSize;
  const int anchorX = cellX * cellSize + cellSize / 2;
  const int anchorZ = cellZ * cellSize + cellSize / 2;
  if (x != anchorX || z != anchorZ)
  {
    return false;
  }

  const int spacingCells =
      std::max(1, cfg.structureMinSpacing / std::max(1, cellSize));
  if ((cellX / spacingCells) != (cellZ / spacingCells))
  {
    return false;
  }

  const int chance = EffectiveChance(cfg.structureChancePerCell,
                                     ctx.Settings.Tuning.structureDensity);
  const uint32_t h = FeatureHash(cellX, cellZ, ctx.Settings.Seed + 7000);
  if (chance <= 0 || h % static_cast<uint32_t>(chance) != 0)
  {
    return false;
  }

  return TryPlacePrefabPool(ctx, x, z, surfaceY, biome, cfg.Structures,
                            PrefabFeaturePool::Structures, 7001, true, false,
                            ctx.Settings.Tuning.structureDensity);
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
  ctx.AccumulateDirtyColumn(surfaceY, surfaceY + 2);
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
  if (FeatureHash(x, z, Seed + 12007) % 4096 != 0)
  {
    return false;
  }
  ctx.World.SetBlock(firePos, ctx.Fire);
  ctx.AccumulateDirtyColumn(surfaceY, surfaceY + 2);
  return true;
}

} // namespace cutum
