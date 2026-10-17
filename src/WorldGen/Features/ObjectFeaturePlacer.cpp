#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/ObjectPlacementConstraints.h"
#include "World/Math/FluidCellState.h"
#include "World/Chunks/ChunkManager.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <array>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

namespace
{

thread_local std::unordered_map<uint64_t, int> ScatterChunkCounts;

uint64_t ChunkXZKey(int chunk_x, int chunk_z)
{
  return (static_cast<uint64_t>(static_cast<uint32_t>(chunk_x)) << 32) ^
         static_cast<uint64_t>(static_cast<uint32_t>(chunk_z));
}

uint32_t FeatureHash(int x, int z, uint32_t Seed)
{
  return ColumnHash(x, z, Seed);
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

float TargetColumnDensity(int spacing, float density_multiplier)
{
  const int effective_spacing = EffectiveSpacing(spacing, density_multiplier);
  if (effective_spacing <= 0)
  {
    return 0.0f;
  }
  const float base = 1.0f / static_cast<float>(std::max(8, effective_spacing));
  const float target = base * (0.5f + density_multiplier * 0.75f);
  return std::clamp(target, 0.012f, 0.07f);
}

bool PassesNoiseDensityGate(int x, int z, uint32_t seed, int spacing,
                            float density_multiplier, uint32_t rule_seed_offset,
                            uint32_t pool_seed_offset)
{
  const int effective_spacing = EffectiveSpacing(spacing, density_multiplier);
  if (effective_spacing <= 0)
  {
    return false;
  }
  const uint32_t column_hash =
      FeatureHash(x, z, seed + pool_seed_offset + rule_seed_offset);
  if (column_hash % static_cast<uint32_t>(effective_spacing) != 0U)
  {
    return false;
  }
  const float noise = NormalizedFBM2D(static_cast<float>(x) * 0.02f,
                                      static_cast<float>(z) * 0.02f,
                                      seed + pool_seed_offset + rule_seed_offset,
                                      2, 0.5f, 2.0f);
  const float noise01 = (noise + 1.0f) * 0.5f;
  return noise01 > 0.28f;
}

int ResolvePlacementYOffset(const WorldGenContext &ctx,
                            const ObjectFeatureRule &rule)
{
  if (rule.PlacementYOffset != 0)
  {
    return rule.PlacementYOffset;
  }
  if (!ctx.Objects)
  {
    return 0;
  }
  const auto prefabOwned = ctx.Objects->GetShared(rule.ObjectName);
  const WorldObjectDefinition *prefab = prefabOwned.get();
  if (!prefab)
  {
    return 0;
  }
  return prefab->PlacementYOffset;
}

PlacementSurfaceInfo ResolveColumnPlacementSurface(const WorldGenContext &ctx,
                                                   int x, int z,
                                                   int heightmapSurfaceY)
{
  return ResolvePlacementSurfaceY(ctx.World, ctx.Registry, x, z,
                                  heightmapSurfaceY, ctx.Settings.MaxHeight,
                                  ctx.Settings.SeaLevel);
}

bool IsColumnReadyForPlantFeatures(const WorldGenContext &ctx, int x, int z,
                                   int heightmapSurfaceY,
                                   PlacementSurfaceInfo &outSurface)
{
  outSurface = ResolveColumnPlacementSurface(ctx, x, z, heightmapSurfaceY);
  if (outSurface.topSolidY < 0)
  {
    return false;
  }
  return IsExposedLandSurface(ctx.World, ctx.Registry, x, z,
                              outSurface.topSolidY);
}

bool PlaceObjectAtWaterSurface(WorldGenContext &ctx, const std::string &prefabName,
                               glm::ivec3 anchorWorldPos)
{
  if (!ctx.Objects)
  {
    return false;
  }
  const auto prefabOwned = ctx.Objects->GetShared(prefabName);
  const WorldObjectDefinition *prefab = prefabOwned.get();
  if (!prefab)
  {
    return false;
  }
  if (!CanPlaceObjectAt(ctx.World, *prefab, anchorWorldPos))
  {
    return false;
  }
  const ObjectPlacementStats stats =
      PlaceObjectAt(ctx.World, ctx.Registry, *prefab, anchorWorldPos, false);
  if (stats.placedCount == 0)
  {
    return false;
  }
  ctx.AccumulateDirtyColumn(stats.minY, stats.maxY);
  return true;
}

bool TryPlaceScatterBlocks(WorldGenContext &ctx, int x, int z, int surfaceY,
                           const ObjectFeatureRule &rule)
{
  const BlockId blockId = ResolvePackScatterBlockId(
      ctx.Registry, ctx.WorldgenOwnerPackId, rule.Scatter.BlockName);
  if (blockId == BLOCK_AIR)
  {
    return false;
  }
  if (!ctx.Registry.HasRenderableTexture(blockId))
  {
    std::cerr << "Scatter: skip scatter block '" << rule.Scatter.BlockName
              << "' (id=" << static_cast<int>(blockId)
              << "): no renderable texture" << std::endl;
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
        ComputeMaxScanY(surfaceY, ctx.Settings.SeaLevel, ctx.Settings.MaxHeight);
    const int localSurface =
        FindTopSolidSurfaceY(ctx.World, ctx.Registry, wx, wz, maxScanY);
    if (localSurface < ctx.Settings.SeaLevel +
                            WorldGenPlacementTuning::MinLandAboveSea)
    {
      continue;
    }
    const int y = localSurface + 1 + rule.Scatter.DyOffset;
    if (y <= ctx.Settings.SeaLevel)
    {
      continue;
    }
    const glm::ivec3 pos(wx, y, wz);
    if (!CanPlacePlantAt(ctx.World, ctx.Registry, pos))
    {
      continue;
    }
    const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(pos);
    const uint64_t chunk_key = ChunkXZKey(chunk_coord.x, chunk_coord.z);
    if (rule.Scatter.MaxPerChunk > 0)
    {
      const int chunk_count = ScatterChunkCounts[chunk_key];
      if (chunk_count >= rule.Scatter.MaxPerChunk)
      {
        continue;
      }
    }
    ctx.World.SetBlock(pos, blockId);
    if (rule.Scatter.MaxPerChunk > 0)
    {
      ++ScatterChunkCounts[chunk_key];
    }
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

bool SubBiomeMatches(SubBiomeId sub_biome,
                     const std::vector<SubBiomeId> &allowed)
{
  if (allowed.empty())
  {
    return true;
  }
  for (SubBiomeId allowed_sub : allowed)
  {
    if (allowed_sub == sub_biome)
    {
      return true;
    }
  }
  return false;
}

float SubBiomePoolWeightMultiplier(SubBiomeId subBiome, BiomeId biome,
                                   ObjectFeaturePool pool)
{
  const std::string biomeId = BiomeIdToString(biome);
  const BiomePackDefinition *packDef =
      UWorldGenPack::BiomeDefinitionFor(biomeId);
  if (packDef &&
      packDef->SubBiomes.count(SubBiomeIdToString(subBiome)) > 0)
  {
    return UWorldGenPack::SubBiomePoolWeightMultiplier(biomeId, subBiome, pool);
  }
  return 1.0f;
}

bool TryPlaceObjectPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                        BiomeId biome,
                        const std::vector<ObjectFeatureRule> &rules,
                        ObjectFeaturePool pool, uint32_t pickSeedOffset,
                        bool useChanceOnly, bool requireTreesEnabled,
                        float densityMultiplier)
{
  if ((requireTreesEnabled && !ctx.Settings.EnableTrees) || !ctx.Objects ||
      rules.empty())
  {
    return false;
  }

  struct Candidate
  {
    const ObjectFeatureRule *Rule;
    int Weight;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(rules.size());
  const uint32_t seed = ctx.Settings.Seed;
  const SubBiomeId subBiome = SubBiomeFor(x, z, biome, seed);
  const float subMul = SubBiomePoolWeightMultiplier(subBiome, biome, pool);

  for (const ObjectFeatureRule &rule : rules)
  {
    if (!BiomeMatches(biome, rule.Biomes))
    {
      continue;
    }
    if (!SubBiomeMatches(subBiome, rule.SubBiomes))
    {
      continue;
    }
    if (rule.Mode == ObjectFeatureRuleMode::ScatterBlocks)
    {
      if (rule.Scatter.BlockName.empty() ||
          ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                    rule.Scatter.BlockName) == BLOCK_AIR)
      {
        continue;
      }
    }
    else if (!ctx.Objects->GetShared(rule.ObjectName))
    {
      if (pool == ObjectFeaturePool::Decoration)
      {
        static std::unordered_set<std::string> warnedMissingDecor;
        if (warnedMissingDecor.insert(rule.ObjectName).second)
        {
          std::cerr << "ObjectFeaturePlacer: missing decoration prefab '"
                    << rule.ObjectName << "'" << std::endl;
        }
      }
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
      if (!PassesNoiseDensityGate(x, z, seed, rule.Spacing, densityMultiplier,
                                  rule.SeedOffset, pickSeedOffset))
      {
        continue;
      }
    }
    const std::string &featureKey =
        rule.Mode == ObjectFeatureRuleMode::ScatterBlocks ? rule.Scatter.BlockName
                                                        : rule.ObjectName;
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
  const ObjectFeatureRule *chosen = candidates.front().Rule;
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
  PlacementSurfaceInfo placementSurface;
  if (!IsColumnReadyForPlantFeatures(ctx, x, z, surfaceY, placementSurface))
  {
    return false;
  }
  const int topSolid = placementSurface.topSolidY;
  if (chosen->Mode == ObjectFeatureRuleMode::ScatterBlocks)
  {
    return TryPlaceScatterBlocks(ctx, x, z, topSolid, *chosen);
  }
  if (chosen->Surface.Kind == SurfaceConstraintKind::WaterSurface)
  {
    if (!SatisfiesSurfaceConstraint(ctx, chosen->Surface, chosen->ObjectName, x,
                                    z, topSolid))
    {
      return false;
    }
    glm::ivec3 water_anchor;
    if (!FindWaterSurfaceAnchorForPlacement(ctx, x, z, water_anchor))
    {
      return false;
    }
    return PlaceObjectAtWaterSurface(ctx, chosen->ObjectName, water_anchor);
  }
  const auto prefabOwned = ctx.Objects->GetShared(chosen->ObjectName);
  const WorldObjectDefinition *prefab = prefabOwned.get();
  if (!prefab)
  {
    return false;
  }
  const glm::ivec3 anchor(
      x, ResolveWorldGenAnchorY(*prefab, ctx.Registry, topSolid, yOffset), z);
  if (!SatisfiesSurfaceConstraint(ctx, chosen->Surface, chosen->ObjectName, x, z,
                                  topSolid))
  {
    return false;
  }
  return PlaceObjectAt(ctx, chosen->ObjectName, anchor, topSolid);
}

bool HasObjectFeatureRules(const ObjectFeatureConfig &cfg)
{
  return !cfg.Vegetation.empty() || !cfg.GroundCover.empty() ||
         !cfg.Decoration.empty() || !cfg.Structures.empty() ||
         !cfg.CaveFeatures.empty();
}

const ObjectFeatureConfig &GetObjectFeatures(const WorldGenContext &ctx)
{
  if (ctx.ObjectFeatures && HasObjectFeatureRules(*ctx.ObjectFeatures))
  {
    return *ctx.ObjectFeatures;
  }
  if (UObjectFeatureConfigStorage::IsLoaded())
  {
    return UObjectFeatureConfigStorage::Get();
  }
  static const ObjectFeatureConfig kEmpty;
  return kEmpty;
}

} // namespace

void ResetScatterChunkCounts()
{
  ScatterChunkCounts.clear();
}

bool CanPlaceObjectAt(const WorldGenContext &ctx, const std::string &prefabName,
                      glm::ivec3 anchorWorldPos)
{
  if (!ctx.Objects)
  {
    return false;
  }
  const auto prefabOwned = ctx.Objects->GetShared(prefabName);
  const WorldObjectDefinition *prefab = prefabOwned.get();
  if (!prefab)
  {
    return false;
  }
  return CanPlaceObjectAt(ctx.World, *prefab, anchorWorldPos);
}

bool PlaceObjectAt(WorldGenContext &ctx, const std::string &prefabName,
                   glm::ivec3 anchorWorldPos, int surfaceY)
{
  if (!ctx.Objects)
  {
    return false;
  }
  const auto prefabOwned = ctx.Objects->GetShared(prefabName);
  const WorldObjectDefinition *prefab = prefabOwned.get();
  if (!prefab)
  {
    return false;
  }
  const int maxScanY = ComputeMaxScanY(surfaceY, ctx.Settings.SeaLevel,
                                       ctx.Settings.MaxHeight);
  const bool canPlace =
      surfaceY >= 0
          ? CanPlaceObjectAtForWorldGen(ctx.World, ctx.Registry, *prefab,
                                        anchorWorldPos, maxScanY,
                                        ctx.Settings.SeaLevel)
          : CanPlaceObjectAt(ctx.World, *prefab, anchorWorldPos);
  if (!canPlace)
  {
    return false;
  }
  const ObjectPlacementStats stats = PlaceObjectAt(
      ctx.World, ctx.Registry, *prefab, anchorWorldPos, false,
      ObjectWorldGenPlacementOptions{maxScanY, ctx.Settings.SeaLevel});
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
  if (!UObjectFeatureConfigStorage::IsLoaded())
  {
    static bool warnedMissingFeatures = false;
    if (!warnedMissingFeatures)
    {
      std::cerr << "object_features missing, vegetation disabled" << std::endl;
      warnedMissingFeatures = true;
    }
    return false;
  }
  if (!ctx.Settings.EnableTrees)
  {
    return false;
  }
  PlacementSurfaceInfo placementSurface;
  if (!IsColumnReadyForPlantFeatures(ctx, x, z, surfaceY, placementSurface))
  {
    return false;
  }
  if (biome == BiomeId::Hills)
  {
    if (!HillsVegetationAllowed(placementSurface.topSolidY, ctx.Settings.SeaLevel,
                                ctx.Settings.MaxHeight))
    {
      return false;
    }
  }
  return TryPlaceObjectPool(ctx, x, z, surfaceY, biome,
                            GetObjectFeatures(ctx).Vegetation,
                            ObjectFeaturePool::Vegetation, 5000, false, true,
                            ctx.Settings.Tuning.vegetationDensity);
}

bool TryPlaceGroundCoverFeatures(WorldGenContext &ctx, int x, int z,
                                 int surfaceY, BiomeId biome,
                                 bool skipIfTreeNearby)
{
  if (skipIfTreeNearby)
  {
    return false;
  }
  if (!UObjectFeatureConfigStorage::IsLoaded() || !ctx.Settings.EnableGroundCover)
  {
    return false;
  }
  PlacementSurfaceInfo placementSurface;
  if (!IsColumnReadyForPlantFeatures(ctx, x, z, surfaceY, placementSurface))
  {
    return false;
  }
  return TryPlaceObjectPool(ctx, x, z, placementSurface.topSolidY, biome,
                            GetObjectFeatures(ctx).GroundCover,
                            ObjectFeaturePool::GroundCover, 5500, false, true,
                            ctx.Settings.Tuning.vegetationDensity * 0.85f);
}

bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome)
{
  if (!UObjectFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableDecoration)
  {
    return false;
  }
  PlacementSurfaceInfo placementSurface;
  if (!IsColumnReadyForPlantFeatures(ctx, x, z, surfaceY, placementSurface))
  {
    return false;
  }
  return TryPlaceObjectPool(ctx, x, z, placementSurface.topSolidY, biome,
                            GetObjectFeatures(ctx).Decoration,
                            ObjectFeaturePool::Decoration, 6000, false, false,
                            ctx.Settings.Tuning.decorationDensity);
}

bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z,
                               int surfaceY, BiomeId biome)
{
  if (!UObjectFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableStructures)
  {
    return false;
  }

  const ObjectFeatureConfig &cfg = GetObjectFeatures(ctx);
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

  constexpr float kMaxStructureSurfaceGradient = 5.5f;
  if (SampleStructureSurfaceGradient(ctx, x, z, surfaceY) >
      kMaxStructureSurfaceGradient)
  {
    return false;
  }

  return TryPlaceObjectPool(ctx, x, z, surfaceY, biome, cfg.Structures,
                            ObjectFeaturePool::Structures, 7001, true, false,
                            ctx.Settings.Tuning.structureDensity);
}

bool TryPlaceCaveFeatures(
    WorldGenContext &ctx, int x, int z, int surfaceY,
    const std::function<BiomeId(int, int, int, int)> &biome_at_3d)
{
  if (!ctx.Settings.EnableCaves || !biome_at_3d || surfaceY < 12)
  {
    return false;
  }
  const ObjectFeatureConfig &config = GetObjectFeatures(ctx);
  if (config.CaveFeatures.empty())
  {
    return false;
  }
  const std::array<BlockId, 7> cave_rock = {
      ctx.Blocks.Stone,
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "granite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "diorite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "andesite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "deepslate"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "tuff"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "rooted_dirt")};
  const auto is_cave_rock = [&cave_rock](BlockId id)
  { return std::find(cave_rock.begin(), cave_rock.end(), id) != cave_rock.end(); };
  const glm::ivec3 current_chunk = UChunkManager::WorldToChunk(glm::ivec3(x, 0, z));

  for (int floor_y = 5; floor_y < surfaceY - 4; ++floor_y)
  {
    const glm::ivec3 floor_pos(x, floor_y, z);
    if (!is_cave_rock(ctx.World.GetBlock(floor_pos)))
    {
      continue;
    }
    const glm::ivec3 first_air(x, floor_y + 1, z);
    if (!ctx.World.IsAir(first_air) ||
        ctx.Registry.IsLiquid(ctx.World.GetBlock(first_air)))
    {
      continue;
    }

    const BiomeId cave_biome = biome_at_3d(x, floor_y + 1, z, surfaceY);
    if (cave_biome != BiomeId::LushCaves &&
        cave_biome != BiomeId::DripstoneCaves &&
        cave_biome != BiomeId::DeepDark)
    {
      continue;
    }
    for (const CaveFeatureRule &rule : config.CaveFeatures)
    {
      if (rule.Biome != cave_biome)
      {
        continue;
      }
      uint32_t hash = ColumnHash(x, z, ctx.Settings.Seed + rule.SeedOffset);
      hash ^= static_cast<uint32_t>(floor_y) * 0x9e3779b9U;
      hash ^= hash >> 16U;
      if (hash % static_cast<uint32_t>(std::max(1, rule.Chance)) != 0U)
      {
        continue;
      }

      const BlockId feature_block = ResolvePackScatterBlockId(
          ctx.Registry, ctx.WorldgenOwnerPackId, rule.BlockName);
      if (feature_block == BLOCK_AIR)
      {
        continue;
      }
      int min_dirty_y = floor_y;
      int max_dirty_y = floor_y + 1;
      bool placed = false;

      if (rule.Kind == CaveFeatureKind::MossPatch)
      {
        const int radius = std::max(0, rule.Radius);
        for (int dz = -radius; dz <= radius; ++dz)
        {
          for (int dx = -radius; dx <= radius; ++dx)
          {
            if (dx * dx + dz * dz > radius * radius)
            {
              continue;
            }
            const glm::ivec3 target_floor(x + dx, floor_y, z + dz);
            if (UChunkManager::WorldToChunk(target_floor) != current_chunk ||
                !is_cave_rock(ctx.World.GetBlock(target_floor)) ||
                !ctx.World.IsAir(target_floor + glm::ivec3(0, 1, 0)))
            {
              continue;
            }
            ctx.World.SetBlock(target_floor, feature_block);
            placed = true;
          }
        }
      }
      else if (rule.Kind == CaveFeatureKind::Stalagmite)
      {
        const int requested_height =
            1 + static_cast<int>((hash >> 8U) % static_cast<uint32_t>(rule.MaxHeight));
        int height = 0;
        for (int dy = 1; dy <= requested_height; ++dy)
        {
          if (!ctx.World.IsAir(glm::ivec3(x, floor_y + dy, z)))
          {
            break;
          }
          height = dy;
        }
        if (height > 0)
        {
          for (int dy = 1; dy <= height; ++dy)
          {
            ctx.World.SetBlock(glm::ivec3(x, floor_y + dy, z), feature_block);
          }
          max_dirty_y = floor_y + height;
          placed = true;
        }
      }
      else if (rule.Kind == CaveFeatureKind::AmethystCluster)
      {
        const glm::ivec3 center(x, floor_y + 1, z);
        if (ctx.World.IsAir(center) &&
            ctx.World.IsAir(center + glm::ivec3(0, 1, 0)))
        {
          ctx.World.SetBlock(center, feature_block);
          if (rule.Radius > 0 && (hash & 1U) != 0U &&
              ctx.World.IsAir(center + glm::ivec3(1, 0, 0)) &&
              UChunkManager::WorldToChunk(center + glm::ivec3(1, 0, 0)) ==
                  current_chunk)
          {
            ctx.World.SetBlock(center + glm::ivec3(1, 0, 0), feature_block);
          }
          placed = true;
        }
      }

      if (placed)
      {
        ctx.AccumulateDirtyColumn(min_dirty_y, max_dirty_y);
        return true;
      }
    }
  }
  return false;
}

bool TryPlaceLavaPool(WorldGenContext &ctx, int x, int z, int surfaceY,
                      BiomeId biome)
{
  if (!ctx.Settings.FillLava || ctx.Blocks.Lava == BLOCK_AIR ||
      biome != BiomeId::Hills)
  {
    return false;
  }
  if (surfaceY <= ctx.Settings.SeaLevel + 2)
  {
    return false;
  }
  for (int dx = -2; dx <= 2; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      for (int y = ctx.Settings.SeaLevel; y <= surfaceY + 2; ++y)
      {
        const glm::ivec3 pos(x + dx, y, z + dz);
        if (ctx.World.GetBlock(pos) == ctx.Blocks.Water)
        {
          return false;
        }
        const BlockId id = ctx.World.GetBlock(pos);
        if (ctx.Registry.IsFluidPermeable(id) &&
            PackFluidCellState(ctx.World.GetFluidState(pos)) != 0)
        {
          return false;
        }
      }
    }
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
      if (below != ctx.Blocks.Stone && below != ctx.Blocks.Gravel)
      {
        return false;
      }
      if (!ctx.World.IsAir(pos))
      {
        return false;
      }
      ctx.World.SetBlock(pos, ctx.Blocks.Lava);
    }
  }
  ctx.AccumulateDirtyColumn(surfaceY, surfaceY + 2);
  return true;
}

bool TryPlaceFirePatch(WorldGenContext &ctx, int x, int z, int surfaceY,
                       BiomeId biome, BlockId grassId)
{
  if (!ctx.Settings.FillFire || ctx.Blocks.Fire == BLOCK_AIR)
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
  ctx.World.SetBlock(firePos, ctx.Blocks.Fire);
  ctx.AccumulateDirtyColumn(surfaceY, surfaceY + 2);
  return true;
}

} // namespace cutum
