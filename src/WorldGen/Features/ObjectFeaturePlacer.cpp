#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Features/ObjectPlacementConstraints.h"
#include "WorldGen/Features/WorldGenFeatureSink.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
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

bool BiomeMatches(BiomeId biome, const ObjectFeatureRule &rule,
                  const BiomeKey &biomeKey)
{
  const std::string key = biomeKey.empty() ? BiomeIdToString(biome) : biomeKey;
  for (const BiomeKey &allowedKey : rule.BiomeKeys)
  {
    if (allowedKey == key)
    {
      return true;
    }
  }
  if (!biomeKey.empty())
  {
    return false;
  }
  for (BiomeId allowedBiome : rule.Biomes)
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
  return std::max(1,
                  static_cast<int>(static_cast<float>(spacing) * multiplier));
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
  return std::max(
      1, static_cast<int>(static_cast<float>(chancePerColumn) * multiplier));
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
  const float noise = NormalizedFBM2D(
      static_cast<float>(x) * 0.02f, static_cast<float>(z) * 0.02f,
      seed + pool_seed_offset + rule_seed_offset, 2, 0.5f, 2.0f);
  const float noise01 = (noise + 1.0f) * 0.5f;
  return noise01 > 0.28f;
}

int FloorDiv(int value, int divisor)
{
  int quotient = value / divisor;
  if (value % divisor < 0)
  {
    --quotient;
  }
  return quotient;
}

bool PassesSpatialFloraCandidateGate(int x, int z, uint32_t seed, int spacing,
                                     float densityMultiplier,
                                     uint32_t rule_seed_offset,
                                     uint32_t pool_seed_offset)
{
  const int effectiveSpacing = EffectiveSpacing(spacing, densityMultiplier);
  if (effectiveSpacing <= 0 || effectiveSpacing == INT_MAX)
  {
    return false;
  }
  const int cellSize = std::clamp(static_cast<int>(std::floor(std::sqrt(
                                      static_cast<float>(effectiveSpacing)))),
                                  3, 16);
  const int cellX = FloorDiv(x, cellSize);
  const int cellZ = FloorDiv(z, cellSize);
  const uint32_t cellHash =
      FeatureHash(cellX, cellZ, seed + pool_seed_offset + rule_seed_offset);
  // The acceptance probability below is normalized against the full cell
  // area. Restricting jitter to an inset silently multiplied the configured
  // density by (jitterSpan / cellSize)^2, making flora substantially rarer
  // than its authored spacing. Sample the whole cell instead, with independent
  // deterministic hashes for the two axes.
  const int candidateX = cellX * cellSize +
                         static_cast<int>(cellHash %
                                          static_cast<uint32_t>(cellSize));
  const uint32_t zHash = FeatureHash(
      cellX, cellZ,
      seed + pool_seed_offset + rule_seed_offset + 0x68bc21ebU);
  const int candidateZ = cellZ * cellSize +
                         static_cast<int>(zHash %
                                          static_cast<uint32_t>(cellSize));
  if (x != candidateX || z != candidateZ)
  {
    return false;
  }

  const int cellArea = cellSize * cellSize;
  const uint32_t acceptance = static_cast<uint32_t>(
      std::clamp(static_cast<int>((static_cast<int64_t>(cellArea) * 10000) /
                                  effectiveSpacing),
                 0, 10000));
  const uint32_t acceptanceHash = FeatureHash(
      cellX, cellZ, seed + pool_seed_offset + rule_seed_offset + 0x5bd1e995U);
  if (acceptanceHash % 10000U >= acceptance)
  {
    return false;
  }

  const float noise = NormalizedFBM2D(
      static_cast<float>(x) * 0.02f, static_cast<float>(z) * 0.02f,
      seed + pool_seed_offset + rule_seed_offset, 2, 0.5f, 2.0f);
  return (noise + 1.0f) * 0.5f > 0.28f;
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

uint64_t TreeVoxelKey(int x, int y, int z)
{
  return (static_cast<uint64_t>(static_cast<uint16_t>(x + 128)) << 32U) |
         (static_cast<uint64_t>(static_cast<uint16_t>(y + 128)) << 16U) |
         static_cast<uint16_t>(z + 128);
}

bool TryPlaceProceduralTree(WorldGenContext &ctx, glm::ivec3 base,
                            const ObjectFeatureRule &rule, uint32_t seed)
{
  const TreeSpeciesProfile &profile = rule.Tree;
  BlockId trunk = ResolvePackScatterBlockId(
      ctx.Registry, ctx.WorldgenOwnerPackId, profile.TrunkBlock);
  BlockId leaves = ResolvePackScatterBlockId(
      ctx.Registry, ctx.WorldgenOwnerPackId, profile.LeavesBlock);
  if (trunk == BLOCK_AIR)
  {
    trunk = ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                      "tree_log");
  }
  if (leaves == BLOCK_AIR)
  {
    leaves = ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                       "tree_leaves");
  }
  if (trunk == BLOCK_AIR || leaves == BLOCK_AIR ||
      !ctx.Registry.HasRenderableTexture(trunk) ||
      !ctx.Registry.HasRenderableTexture(leaves))
  {
    return false;
  }
  if (base.y < ctx.Settings.MinHeight || base.y >= ctx.Settings.MaxHeight ||
      !CanPlacePlantAt(ctx.World, ctx.Registry, base))
  {
    return false;
  }

  const uint32_t heightHash = FeatureHash(base.x, base.z, seed + 0x31415927U);
  const int heightRange = profile.HeightMax - profile.HeightMin + 1;
  const int height =
      profile.HeightMin +
      static_cast<int>(heightHash % static_cast<uint32_t>(heightRange));
  const int radiusX = std::clamp(
      profile.CrownRadiusX + static_cast<int>((heightHash >> 8U) % 3U) - 1, 1,
      9);
  const int radiusZ = std::clamp(
      profile.CrownRadiusZ + static_cast<int>((heightHash >> 11U) % 3U) - 1, 1,
      9);
  const int crownHeight = std::clamp(
      profile.CrownHeight + static_cast<int>((heightHash >> 14U) % 3U) - 1, 2,
      18);
  const float leafDensity = std::clamp(
      profile.LeafDensity +
          static_cast<float>((heightHash >> 17U) % 13U) / 100.0f - 0.06f,
      0.2f, 1.0f);

  std::vector<std::pair<glm::ivec3, BlockId>> voxels;
  std::unordered_map<uint64_t, size_t> voxelIndex;
  const auto addVoxel = [&](int x, int y, int z, BlockId block)
  {
    const uint64_t key = TreeVoxelKey(x, y, z);
    const auto it = voxelIndex.find(key);
    if (it == voxelIndex.end())
    {
      voxelIndex.emplace(key, voxels.size());
      voxels.emplace_back(glm::ivec3(x, y, z), block);
    }
    else if (block == trunk)
    {
      voxels[it->second].second = trunk;
    }
  };

  for (int y = 0; y < height; ++y)
  {
    addVoxel(0, y, 0, trunk);
  }

  if (profile.RootChance > 0.0f)
  {
    const uint32_t rootHash = FeatureHash(base.x, base.z, seed + 0x524f4f54U);
    if (static_cast<float>(rootHash % 10000U) / 10000.0f < profile.RootChance)
    {
      static constexpr int kRootX[4] = {1, 0, -1, 0};
      static constexpr int kRootZ[4] = {0, 1, 0, -1};
      const int rotation = static_cast<int>((rootHash >> 8U) % 4U);
      const int rootLength = 1 + static_cast<int>((rootHash >> 12U) % 2U);
      for (int arm = 0; arm < 4; ++arm)
      {
        if (((rootHash >> (arm + 16U)) & 1U) == 0U)
        {
          continue;
        }
        const int dir = (arm + rotation) % 4;
        for (int distance = 1; distance <= rootLength; ++distance)
        {
          addVoxel(kRootX[dir] * distance, 0, kRootZ[dir] * distance, trunk);
        }
      }
    }
  }

  const bool conicalCrown = profile.CrownShape == TreeCrownShape::Conical;
  // Conifers carry foliage along the lower trunk. Broadleaf crowns remain
  // centered near the top, while conical profiles taper from the lower crown
  // base to the top of the trunk.
  const int crownBase = conicalCrown
                            ? std::max(1, height - crownHeight)
                            : std::max(1, height - crownHeight / 2);
  const int crownTop = conicalCrown
                           ? std::max(crownBase, height - 1)
                           : height + std::max(1, crownHeight / 2);
  for (int y = crownBase; y <= crownTop; ++y)
  {
    const float t = static_cast<float>(y - crownBase) /
                    static_cast<float>(std::max(1, crownTop - crownBase));
    float shape = 1.0f;
    switch (profile.CrownShape)
    {
    case TreeCrownShape::Conical:
      shape = 1.0f - t * 0.82f;
      break;
    case TreeCrownShape::Umbrella:
      shape = t < 0.62f ? 0.5f + t * 0.55f : 0.84f - (t - 0.62f) * 0.28f;
      break;
    case TreeCrownShape::Columnar:
      shape = 0.76f + 0.12f * std::sin(t * 3.14159265f);
      break;
    case TreeCrownShape::Round:
    default:
    {
      const float normalized = 2.0f * t - 1.0f;
      shape = std::sqrt(std::max(0.08f, 1.0f - normalized * normalized));
      break;
    }
    }
    const int rx = std::max(1, static_cast<int>(std::lround(radiusX * shape)));
    const int rz = std::max(1, static_cast<int>(std::lround(radiusZ * shape)));
    for (int dz = -rz; dz <= rz; ++dz)
    {
      for (int dx = -rx; dx <= rx; ++dx)
      {
        const float ellipse =
            static_cast<float>(dx * dx) / static_cast<float>(rx * rx) +
            static_cast<float>(dz * dz) / static_cast<float>(rz * rz);
        constexpr float kTau = 6.28318530718f;
        const float angle =
            std::atan2(static_cast<float>(dz), static_cast<float>(dx));
        const int sector = static_cast<int>(
            std::floor((angle + 3.14159265359f) * (16.0f / kTau)));
        const uint32_t contourHash =
            FeatureHash(sector, y - crownBase, seed + 0x43524f57U);
        const float contour =
            (static_cast<float>(contourHash % 2001U) / 1000.0f - 1.0f) *
            profile.CrownIrregularity;
        if (ellipse > 1.0f + contour)
        {
          continue;
        }
        uint32_t leafHash = FeatureHash(base.x + dx, base.z + dz,
                                        seed + static_cast<uint32_t>(y * 7919));
        leafHash ^= static_cast<uint32_t>(y) * 0x9e3779b9U;
        const float leafRoll = static_cast<float>(leafHash % 10000U) / 10000.0f;
        const bool crownTip = y == crownTop && dx == 0 && dz == 0;
        if (!crownTip && leafRoll > leafDensity)
        {
          continue;
        }
        addVoxel(dx, y, dz, leaves);
      }
    }
  }

  // Add offset foliage clumps around the main crown so broadleaf silhouettes
  // do not all read as one perfectly centered ellipsoid.
  for (int lobe = 0; lobe < profile.CrownLobes; ++lobe)
  {
    const uint32_t lobeHash =
        FeatureHash(base.x + lobe * 17, base.z - lobe * 29,
                    seed + 0x4c4f4245U + static_cast<uint32_t>(lobe * 97));
    const int direction = static_cast<int>((lobe * 3 + lobeHash % 8U) % 8U);
    static constexpr int kLobeDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int kLobeDirZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    const float offsetScale =
        0.38f + static_cast<float>((lobeHash >> 8U) % 31U) / 100.0f;
    const int centerX = static_cast<int>(std::lround(
        static_cast<float>(kLobeDirX[direction]) * radiusX * offsetScale));
    const int centerZ = static_cast<int>(std::lround(
        static_cast<float>(kLobeDirZ[direction]) * radiusZ * offsetScale));
    const int crownSpan = std::max(1, crownTop - crownBase);
    const int centerY =
        crownBase +
        std::clamp(static_cast<int>(
                       (static_cast<float>((lobeHash >> 16U) % 100U) / 100.0f) *
                       crownSpan),
                   0, crownSpan);
    const int lobeRx =
        std::max(1, static_cast<int>(std::lround(radiusX * 0.58f)));
    const int lobeRz =
        std::max(1, static_cast<int>(std::lround(radiusZ * 0.58f)));
    const int lobeRy = std::max(1, crownHeight / 3);
    for (int dy = -lobeRy; dy <= lobeRy; ++dy)
    {
      for (int dz = -lobeRz; dz <= lobeRz; ++dz)
      {
        for (int dx = -lobeRx; dx <= lobeRx; ++dx)
        {
          const float distance =
              static_cast<float>(dx * dx) /
                  static_cast<float>(lobeRx * lobeRx) +
              static_cast<float>(dy * dy) /
                  static_cast<float>(lobeRy * lobeRy) +
              static_cast<float>(dz * dz) / static_cast<float>(lobeRz * lobeRz);
          if (distance > 1.0f + profile.CrownIrregularity * 0.35f)
          {
            continue;
          }
          const uint32_t lobeLeafHash =
              FeatureHash(base.x + centerX + dx, base.z + centerZ + dz,
                          seed + static_cast<uint32_t>((centerY + dy) * 7919) +
                              lobe * 313U);
          if (static_cast<float>(lobeLeafHash % 10000U) / 10000.0f <=
              leafDensity)
          {
            addVoxel(centerX + dx, centerY + dy, centerZ + dz, leaves);
          }
        }
      }
    }
  }

  if (conicalCrown)
  {
    static constexpr int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int kDirZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    const int tiers = profile.BranchCount;
    for (int tier = 0; tier < tiers; ++tier)
    {
      const uint32_t tierHash = FeatureHash(
          base.x + tier * 13, base.z - tier * 7,
          seed + 0x4252414eU + static_cast<uint32_t>(tier * 31));
      const float tierT = static_cast<float>(tier + 1) /
                          static_cast<float>(tiers + 1);
      const int branchY = crownBase + static_cast<int>(std::lround(
                                           tierT * (crownTop - crownBase)));
      const float taper = 1.0f - tierT * 0.82f;
      const int branchLength = std::max(
          1, static_cast<int>(std::lround(
                 static_cast<float>(std::max(radiusX, radiusZ)) * taper *
                 (0.55f + static_cast<float>((tierHash >> 16U) % 31U) /
                               100.0f))));
      // Choose cardinal or diagonal directions per tier; the per-tree rotation
      // varies the silhouette without breaking the ring-like growth pattern.
      const int rotation = static_cast<int>((tierHash >> 8U) & 1U);
      for (int arm = 0; arm < 4; ++arm)
      {
        const int direction = (arm * 2 + rotation) % 8;
        int tipX = 0;
        int tipZ = 0;
        for (int step = 1; step <= branchLength; ++step)
        {
          tipX = kDirX[direction] * step;
          tipZ = kDirZ[direction] * step;
          const int branchStepY =
              std::min(height - 1, branchY + (step + 2) / 3);
          addVoxel(tipX, branchStepY, tipZ, trunk);
        }
        const int leafY = std::min(height, branchY + branchLength / 3);
        for (int dy = -1; dy <= 1; ++dy)
        {
          for (int dz = -1; dz <= 1; ++dz)
          {
            for (int dx = -1; dx <= 1; ++dx)
            {
              if (std::abs(dx) + std::abs(dz) + std::abs(dy) <= 2)
              {
                addVoxel(tipX + dx, leafY + dy, tipZ + dz, leaves);
              }
            }
          }
        }
      }
    }
  }
  else
  {
    for (int branch = 0; branch < profile.BranchCount; ++branch)
    {
      const uint32_t branchHash =
          FeatureHash(base.x + branch * 13, base.z - branch * 7,
                      seed + 0x4252414eU + static_cast<uint32_t>(branch * 31));
      const int direction = static_cast<int>(branchHash % 8U);
      static constexpr int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
      static constexpr int kDirZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
      const int branchY =
          height / 2 +
          static_cast<int>((branchHash >> 8U) %
                           static_cast<uint32_t>(std::max(1, height / 3)));
      const int branchLengthMax = std::max({2, radiusX, radiusZ});
      const int branchLengthMin = std::max(1, branchLengthMax / 2);
      const int branchLengthRange = branchLengthMax - branchLengthMin + 1;
      const int length =
          branchLengthMin + static_cast<int>((branchHash >> 16U) %
                                             static_cast<uint32_t>(branchLengthRange));
      int tipX = 0;
      int tipZ = 0;
      for (int step = 1; step <= length; ++step)
      {
        tipX = kDirX[direction] * step;
        tipZ = kDirZ[direction] * step;
        addVoxel(tipX, branchY + (step + 1) / 2, tipZ, trunk);
      }
      const int leafY = branchY + (length + 1) / 2;
      for (int dy = -1; dy <= 1; ++dy)
      {
        for (int dz = -1; dz <= 1; ++dz)
        {
          for (int dx = -1; dx <= 1; ++dx)
          {
            if (std::abs(dx) + std::abs(dz) + std::abs(dy) <= 2)
            {
              addVoxel(tipX + dx, leafY + dy, tipZ + dz, leaves);
            }
          }
        }
      }
    }
  }

  int minY = base.y;
  int maxY = base.y;
  std::vector<std::pair<glm::ivec3, BlockId>> accepted;
  accepted.reserve(voxels.size());
  for (const auto &[relative, block] : voxels)
  {
    const glm::ivec3 worldPos = base + relative;
    if (worldPos.y < ctx.Settings.MinHeight ||
        worldPos.y >= ctx.Settings.MaxHeight)
    {
      return false;
    }
    const BlockId current = ctx.World.GetBlock(worldPos);
    if (!ctx.World.IsAir(worldPos) && current != leaves)
    {
      const bool lateralVoxel = relative.x != 0 || relative.z != 0;
      if (block != trunk || lateralVoxel)
      {
        // Canopies, roots, and lateral branches may overlap terrain on slopes
        // or meet another feature. Trim those voxels; the central trunk stays
        // required.
        continue;
      }
      return false;
    }
    accepted.emplace_back(relative, block);
    minY = std::min(minY, worldPos.y);
    maxY = std::max(maxY, worldPos.y);
  }
  VoxelFeatureSink sink(ctx.World);
  for (const auto &[relative, block] : accepted)
  {
    sink.WriteVoxel(base + relative, block);
  }
  ctx.AccumulateDirtyColumn(minY, maxY + 1);
  return !accepted.empty();
}

bool TryPlaceProceduralShrub(WorldGenContext &ctx, glm::ivec3 base,
                             const ObjectFeatureRule &rule, uint32_t seed)
{
  const ShrubSpeciesProfile &profile = rule.Shrub;
  BlockId leaves = ResolvePackScatterBlockId(
      ctx.Registry, ctx.WorldgenOwnerPackId, profile.LeavesBlock);
  BlockId stem =
      profile.StemBlock.empty()
          ? BLOCK_AIR
          : ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                      profile.StemBlock);
  BlockId accent =
      profile.AccentBlock.empty()
          ? BLOCK_AIR
          : ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                      profile.AccentBlock);
  if (leaves == BLOCK_AIR)
  {
    leaves = ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                       "tree_leaves");
  }
  if (stem != BLOCK_AIR && !ctx.Registry.HasRenderableTexture(stem))
  {
    stem = BLOCK_AIR;
  }
  if (accent != BLOCK_AIR && !ctx.Registry.HasRenderableTexture(accent))
  {
    accent = BLOCK_AIR;
  }
  if (leaves == BLOCK_AIR || !ctx.Registry.HasRenderableTexture(leaves) ||
      base.y < ctx.Settings.MinHeight || base.y >= ctx.Settings.MaxHeight ||
      !CanPlacePlantAt(ctx.World, ctx.Registry, base))
  {
    return false;
  }

  const uint32_t shapeHash = FeatureHash(base.x, base.z, seed + 0x53485255U);
  const int heightRange = profile.HeightMax - profile.HeightMin + 1;
  const int height =
      profile.HeightMin +
      static_cast<int>(shapeHash % static_cast<uint32_t>(heightRange));
  const int radiusX = std::clamp(
      profile.RadiusX + static_cast<int>((shapeHash >> 8U) % 3U) - 1, 1, 7);
  const int radiusZ = std::clamp(
      profile.RadiusZ + static_cast<int>((shapeHash >> 11U) % 3U) - 1, 1, 7);
  const float leafDensity = std::clamp(
      profile.LeafDensity +
          static_cast<float>((shapeHash >> 15U) % 11U) / 100.0f - 0.05f,
      0.2f, 1.0f);

  std::vector<std::pair<glm::ivec3, BlockId>> voxels;
  std::unordered_map<uint64_t, size_t> voxelIndex;
  const auto addVoxel = [&](int x, int y, int z, BlockId block)
  {
    if (block == BLOCK_AIR)
    {
      return;
    }
    const uint64_t key = TreeVoxelKey(x, y, z);
    const auto it = voxelIndex.find(key);
    if (it == voxelIndex.end())
    {
      voxelIndex.emplace(key, voxels.size());
      voxels.emplace_back(glm::ivec3(x, y, z), block);
      return;
    }
    if (block == stem ||
        (block == accent && voxels[it->second].second == leaves))
    {
      voxels[it->second].second = block;
    }
  };

  const auto addLeafClump = [&](int centerX, int centerY, int centerZ,
                                int clumpRadiusX, int clumpRadiusY,
                                int clumpRadiusZ, uint32_t clumpSeed)
  {
    for (int dy = -clumpRadiusY; dy <= clumpRadiusY; ++dy)
    {
      if (centerY + dy < 0)
      {
        continue;
      }
      for (int dz = -clumpRadiusZ; dz <= clumpRadiusZ; ++dz)
      {
        for (int dx = -clumpRadiusX; dx <= clumpRadiusX; ++dx)
        {
          const float distance =
              static_cast<float>(dx * dx) /
                  static_cast<float>(clumpRadiusX * clumpRadiusX) +
              static_cast<float>(dy * dy) /
                  static_cast<float>(clumpRadiusY * clumpRadiusY) +
              static_cast<float>(dz * dz) /
                  static_cast<float>(clumpRadiusZ * clumpRadiusZ);
          const float angle =
              std::atan2(static_cast<float>(dz), static_cast<float>(dx));
          const int sector = static_cast<int>(
              std::floor((angle + 3.14159265359f) * (8.0f / 6.28318530718f)));
          const uint32_t contourHash = FeatureHash(
              sector, dy + clumpRadiusY, seed + clumpSeed + 0x434c554dU);
          const float contour =
              (static_cast<float>(contourHash % 2001U) / 1000.0f - 1.0f) *
              profile.CrownIrregularity;
          if (distance > 1.0f + contour)
          {
            continue;
          }
          const uint32_t leafHash = FeatureHash(
              base.x + centerX + dx, base.z + centerZ + dz,
              seed + static_cast<uint32_t>((centerY + dy) * 7919) + clumpSeed);
          if (static_cast<float>(leafHash % 10000U) / 10000.0f <= leafDensity)
          {
            addVoxel(centerX + dx, centerY + dy, centerZ + dz, leaves);
          }
        }
      }
    }
  };

  const int coreY = std::max(1, height / 2);
  addLeafClump(0, coreY, 0, radiusX, std::max(1, height / 2), radiusZ,
               0x434f5245U);
  for (int lobe = 0; lobe < profile.LobeCount; ++lobe)
  {
    const uint32_t lobeHash =
        FeatureHash(base.x + lobe * 23, base.z - lobe * 31,
                    seed + 0x4c4f4245U + static_cast<uint32_t>(lobe * 113));
    const int direction = static_cast<int>(
        (lobe * std::max(1, 8 / profile.LobeCount) + lobeHash % 3U) % 8U);
    static constexpr int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int kDirZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    const float offset =
        0.48f + static_cast<float>((lobeHash >> 8U) % 25U) / 100.0f;
    const int centerX =
        static_cast<int>(std::lround(kDirX[direction] * radiusX * offset));
    const int centerZ =
        static_cast<int>(std::lround(kDirZ[direction] * radiusZ * offset));
    const int centerY = std::clamp(
        coreY + static_cast<int>((lobeHash >> 16U) % 3U) - 1, 0, height);
    addLeafClump(centerX, centerY, centerZ,
                 std::max(1, static_cast<int>(std::lround(radiusX * 0.56f))),
                 std::max(1, height / 3),
                 std::max(1, static_cast<int>(std::lround(radiusZ * 0.56f))),
                 static_cast<uint32_t>(lobe * 313U));
  }

  if (stem != BLOCK_AIR)
  {
    const int stemHeight = std::max(1, (height + 1) / 2);
    for (int y = 0; y < stemHeight; ++y)
    {
      addVoxel(0, y, 0, stem);
    }
  }

  if (accent != BLOCK_AIR && profile.AccentChance > 0.0f)
  {
    for (auto &voxel : voxels)
    {
      if (voxel.second != leaves)
      {
        continue;
      }
      const glm::ivec3 worldPos = base + voxel.first;
      const uint32_t accentHash = FeatureHash(
          worldPos.x, worldPos.z,
          seed + static_cast<uint32_t>(worldPos.y * 7919) + 0x42455252U);
      if (static_cast<float>(accentHash % 10000U) / 10000.0f <
          profile.AccentChance)
      {
        voxel.second = accent;
      }
    }
  }

  int minY = base.y;
  int maxY = base.y;
  std::vector<std::pair<glm::ivec3, BlockId>> accepted;
  accepted.reserve(voxels.size());
  for (const auto &[relative, block] : voxels)
  {
    const glm::ivec3 worldPos = base + relative;
    if (worldPos.y < ctx.Settings.MinHeight ||
        worldPos.y >= ctx.Settings.MaxHeight)
    {
      if (block != stem)
      {
        continue;
      }
      return false;
    }
    const BlockId current = ctx.World.GetBlock(worldPos);
    if (!ctx.World.IsAir(worldPos) && current != leaves && current != accent &&
        current != stem)
    {
      if (block == leaves || block == accent)
      {
        // Leaf voxels intersecting a hillside or another feature are clipped.
        // A single collision should not discard the whole shrub.
        continue;
      }
      return false;
    }
    accepted.emplace_back(relative, block);
    minY = std::min(minY, worldPos.y);
    maxY = std::max(maxY, worldPos.y);
  }

  if (accepted.empty())
  {
    return false;
  }

  VoxelFeatureSink sink(ctx.World);
  for (const auto &[relative, block] : accepted)
  {
    sink.WriteVoxel(base + relative, block);
  }
  ctx.AccumulateDirtyColumn(minY, maxY + 1);
  return true;
}

PlacementSurfaceInfo ResolveColumnPlacementSurface(const WorldGenContext &ctx,
                                                   int x, int z,
                                                   int heightmapSurfaceY)
{
  return ResolvePlacementSurfaceY(ctx.World, ctx.Registry, x, z,
                                  heightmapSurfaceY, ctx.Settings.MaxHeight,
                                  ctx.Settings.SeaLevel,
                                  ctx.Settings.MinHeight);
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

bool PlaceObjectAtWaterSurface(WorldGenContext &ctx,
                               const std::string &prefabName,
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
  VoxelFeatureSink sink(ctx.World);
  const ObjectPlacementStats stats =
      PlaceObjectAt(ctx.World, ctx.Registry, *prefab, anchorWorldPos, false, {},
                    [&sink](glm::ivec3 position, BlockId block)
                    { sink.WriteVoxel(position, block); });
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
  VoxelFeatureSink sink(ctx.World);

  for (int attempt = 0; attempt < rule.Scatter.Attempts; ++attempt)
  {
    const uint32_t h = FeatureHash(
        x, z, seed + rule.SeedOffset + static_cast<uint32_t>(attempt * 17));
    const int ox = static_cast<int>(h % static_cast<uint32_t>(span)) - radius;
    const int oz =
        static_cast<int>((h / 31U) % static_cast<uint32_t>(span)) - radius;
    const int wx = x + ox;
    const int wz = z + oz;
    glm::ivec3 pos;
    if (rule.Surface.Kind == SurfaceConstraintKind::UnderwaterFloor)
    {
      glm::ivec3 waterAnchor;
      if (!FindUnderwaterFloorAnchorForPlacement(ctx, wx, wz, waterAnchor))
      {
        continue;
      }
      pos = waterAnchor + glm::ivec3(0, rule.Scatter.DyOffset, 0);
      const BlockId support = ctx.World.GetBlock(pos - glm::ivec3(0, 1, 0));
      if (ctx.World.GetBlock(pos) != ctx.Blocks.Water || support == BLOCK_AIR ||
          !ctx.Registry.BlocksMovement(support) ||
          ctx.Registry.IsLiquid(support))
      {
        continue;
      }
    }
    else
    {
      const int maxScanY = ComputeMaxScanY(surfaceY, ctx.Settings.SeaLevel,
                                           ctx.Settings.MaxHeight);
      const int localSurface =
          FindTopSolidSurfaceY(ctx.World, ctx.Registry, wx, wz, maxScanY,
                               ctx.Settings.MinHeight);
      if (localSurface <
          ctx.Settings.SeaLevel + WorldGenPlacementTuning::MinLandAboveSea)
      {
        continue;
      }
      const int y = localSurface + 1 + rule.Scatter.DyOffset;
      if (y <= ctx.Settings.SeaLevel)
      {
        continue;
      }
      pos = glm::ivec3(wx, y, wz);
      if (!CanPlacePlantAt(ctx.World, ctx.Registry, pos))
      {
        continue;
      }
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
    sink.WriteVoxel(pos, blockId);
    if (rule.Scatter.MaxPerChunk > 0)
    {
      ++ScatterChunkCounts[chunk_key];
    }
    ++placed;
    minY = std::min(minY, pos.y);
    maxY = std::max(maxY, pos.y);
  }

  if (placed <= 0)
  {
    return false;
  }
  ctx.AccumulateDirtyColumn(minY, maxY);
  return true;
}

class ScatterBlocksPlacementStrategy final
    : public IObjectFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, int x, int z, int surfaceY,
             const ObjectFeatureRule &rule) const override
  {
    if (!SatisfiesSurfaceConstraint(ctx, rule.Surface, rule.Scatter.BlockName,
                                    x, z, surfaceY))
    {
      return false;
    }
    return TryPlaceScatterBlocks(ctx, x, z, surfaceY, rule);
  }
};

class ProceduralTreePlacementStrategy final
    : public IObjectFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, int x, int z, int surfaceY,
             const ObjectFeatureRule &rule) const override
  {
    if (!SatisfiesSurfaceConstraint(ctx, rule.Surface, rule.ObjectName, x, z,
                                    surfaceY))
    {
      return false;
    }
    return TryPlaceProceduralTree(
        ctx, glm::ivec3(x, surfaceY + 1, z), rule,
        FeatureHash(x, z, ctx.Settings.Seed + rule.SeedOffset));
  }
};

class ProceduralShrubPlacementStrategy final
    : public IObjectFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, int x, int z, int surfaceY,
             const ObjectFeatureRule &rule) const override
  {
    if (!SatisfiesSurfaceConstraint(ctx, rule.Surface, rule.ObjectName, x, z,
                                    surfaceY))
    {
      return false;
    }
    return TryPlaceProceduralShrub(
        ctx, glm::ivec3(x, surfaceY + 1, z), rule,
        FeatureHash(x, z, ctx.Settings.Seed + rule.SeedOffset));
  }
};

class ObjectPrefabPlacementStrategy final
    : public IObjectFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, int x, int z, int surfaceY,
             const ObjectFeatureRule &rule) const override
  {
    if (!SatisfiesSurfaceConstraint(ctx, rule.Surface, rule.ObjectName, x, z,
                                    surfaceY))
    {
      return false;
    }

    if (rule.Surface.Kind == SurfaceConstraintKind::WaterSurface)
    {
      glm::ivec3 waterAnchor;
      return FindWaterSurfaceAnchorForPlacement(ctx, x, z, waterAnchor,
                                                rule.Surface.NearWaterRadius) &&
             PlaceObjectAtWaterSurface(ctx, rule.ObjectName, waterAnchor);
    }

    if (!ctx.Objects)
    {
      return false;
    }
    const auto prefabOwned = ctx.Objects->GetShared(rule.ObjectName);
    const WorldObjectDefinition *prefab = prefabOwned.get();
    if (!prefab)
    {
      return false;
    }
    const int yOffset = ResolvePlacementYOffset(ctx, rule);
    const glm::ivec3 anchor(
        x, ResolveWorldGenAnchorY(*prefab, ctx.Registry, surfaceY, yOffset), z);
    return PlaceObjectAt(ctx, rule.ObjectName, anchor, surfaceY);
  }
};

void EnsureBuiltinFeaturePlacementStrategies()
{
  static std::once_flag registered;
  std::call_once(registered,
                 []
                 {
                   ObjectFeaturePlacementStrategyRegistry::Register(
                       ObjectFeatureRuleMode::Object,
                       std::make_shared<ObjectPrefabPlacementStrategy>());
                   ObjectFeaturePlacementStrategyRegistry::Register(
                       ObjectFeatureRuleMode::ScatterBlocks,
                       std::make_shared<ScatterBlocksPlacementStrategy>());
                   ObjectFeaturePlacementStrategyRegistry::Register(
                       ObjectFeatureRuleMode::ProceduralTree,
                       std::make_shared<ProceduralTreePlacementStrategy>());
                   ObjectFeaturePlacementStrategyRegistry::Register(
                       ObjectFeatureRuleMode::ProceduralShrub,
                       std::make_shared<ProceduralShrubPlacementStrategy>());
                 });
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
  if (packDef && packDef->SubBiomes.count(SubBiomeIdToString(subBiome)) > 0)
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
                        float densityMultiplier, const BiomeKey &biomeKey)
{
  if ((requireTreesEnabled && !ctx.Settings.EnableTrees) || rules.empty())
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
  const float subMul = biomeKey.empty()
                           ? SubBiomePoolWeightMultiplier(subBiome, biome, pool)
                           : 1.0f;
  PlacementSurfaceInfo landSurface;
  const bool hasLandSurface =
      IsColumnReadyForPlantFeatures(ctx, x, z, surfaceY, landSurface);

  for (const ObjectFeatureRule &rule : rules)
  {
    if (ctx.Settings.WorldGenProfileVersion < 2 &&
        (rule.Surface.Kind == SurfaceConstraintKind::WaterSurface ||
         rule.Surface.Kind == SurfaceConstraintKind::UnderwaterFloor))
    {
      continue;
    }
    if (rule.Mode == ObjectFeatureRuleMode::ProceduralTree &&
        ctx.Settings.WorldGenProfileVersion < 2)
    {
      continue;
    }
    if (rule.Mode == ObjectFeatureRuleMode::ProceduralShrub &&
        ctx.Settings.WorldGenProfileVersion < 2)
    {
      continue;
    }
    if (!BiomeMatches(biome, rule, biomeKey))
    {
      continue;
    }
    if (!SubBiomeMatches(subBiome, rule.SubBiomes))
    {
      continue;
    }

    int ecologySurfaceY = surfaceY;
    if (rule.Surface.Kind == SurfaceConstraintKind::WaterSurface)
    {
      glm::ivec3 waterAnchor;
      if (!FindWaterSurfaceAnchorForPlacement(ctx, x, z, waterAnchor,
                                              rule.Surface.NearWaterRadius))
      {
        continue;
      }
      ecologySurfaceY = waterAnchor.y - 1;
    }
    else if (rule.Surface.Kind == SurfaceConstraintKind::UnderwaterFloor)
    {
      glm::ivec3 underwaterAnchor;
      if (!FindUnderwaterFloorAnchorForPlacement(ctx, x, z, underwaterAnchor))
      {
        continue;
      }
      ecologySurfaceY = underwaterAnchor.y - 1;
    }
    else
    {
      if (!hasLandSurface)
      {
        continue;
      }
      ecologySurfaceY = landSurface.topSolidY;
    }
    if (rule.Surface.Kind != SurfaceConstraintKind::WaterSurface &&
        rule.Surface.Kind != SurfaceConstraintKind::UnderwaterFloor &&
        !SatisfiesSurfaceConstraint(ctx, rule.Surface,
                                    rule.Mode ==
                                            ObjectFeatureRuleMode::ScatterBlocks
                                        ? rule.Scatter.BlockName
                                        : rule.ObjectName,
                                    x, z, ecologySurfaceY))
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
    else if (rule.Mode == ObjectFeatureRuleMode::Object &&
             (!ctx.Objects || !ctx.Objects->GetShared(rule.ObjectName)))
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
      const int chance =
          EffectiveChance(rule.ChancePerColumn, densityMultiplier);
      if (chance <= 0 || h % static_cast<uint32_t>(chance) != 0)
      {
        continue;
      }
    }
    else
    {
      const bool floraPool = ctx.Settings.WorldGenProfileVersion >= 2 &&
                             (pool == ObjectFeaturePool::Vegetation ||
                              pool == ObjectFeaturePool::GroundCover);
      const bool passes =
          floraPool
              ? PassesSpatialFloraCandidateGate(x, z, seed, rule.Spacing,
                                                densityMultiplier,
                                                rule.SeedOffset, pickSeedOffset)
              : PassesNoiseDensityGate(x, z, seed, rule.Spacing,
                                       densityMultiplier, rule.SeedOffset,
                                       pickSeedOffset);
      if (!passes)
      {
        continue;
      }
    }
    const FeatureEcologyProfile &ecology = rule.Ecology;
    if (!ecology.AllowsElevation(ecologySurfaceY, ctx.Settings.SeaLevel))
    {
      continue;
    }
    if (ecology.HasTemperatureRange || ecology.HasMoistureRange)
    {
      const ClimateSample climate = SampleClimate(x, z, seed);
      if ((ecology.HasTemperatureRange &&
           (climate.temperature < ecology.TemperatureMin ||
            climate.temperature > ecology.TemperatureMax)) ||
          (ecology.HasMoistureRange &&
           (climate.moisture < ecology.MoistureMin ||
            climate.moisture > ecology.MoistureMax)))
      {
        continue;
      }
    }
    if (ecology.MaxSlope > 0.0f &&
        SampleStructureSurfaceGradient(ctx, x, z, ecologySurfaceY) >
            ecology.MaxSlope)
    {
      continue;
    }
    const std::string &featureKey =
        rule.Mode == ObjectFeatureRuleMode::ScatterBlocks
            ? rule.Scatter.BlockName
            : rule.ObjectName;
    const float packMul = UWorldGenPack::FeatureWeightMultiplier(
        biomeKey.empty() ? BiomeIdToString(biome) : biomeKey, featureKey);
    if (packMul <= 0.0f)
    {
      continue;
    }
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
  int pick = static_cast<int>(FeatureHash(x, z, seed + pickSeedOffset) %
                              static_cast<uint32_t>(totalWeight));
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

  int strategySurfaceY = surfaceY;
  if (chosen->Surface.Kind == SurfaceConstraintKind::WaterSurface)
  {
    glm::ivec3 anchor;
    if (!FindWaterSurfaceAnchorForPlacement(ctx, x, z, anchor,
                                            chosen->Surface.NearWaterRadius))
    {
      return false;
    }
    strategySurfaceY = anchor.y - 1;
  }
  else if (chosen->Surface.Kind == SurfaceConstraintKind::UnderwaterFloor)
  {
    glm::ivec3 anchor;
    if (!FindUnderwaterFloorAnchorForPlacement(ctx, x, z, anchor))
    {
      return false;
    }
    strategySurfaceY = anchor.y - 1;
  }
  else
  {
    if (!hasLandSurface)
    {
      return false;
    }
    strategySurfaceY = landSurface.topSolidY;
  }
  EnsureBuiltinFeaturePlacementStrategies();
  const auto strategy =
      ObjectFeaturePlacementStrategyRegistry::Find(chosen->Mode);
  return strategy && strategy->Place(ctx, x, z, strategySurfaceY, *chosen);
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

void ResetScatterChunkCounts() { ScatterChunkCounts.clear(); }

void RegisterBuiltinObjectFeaturePlacementStrategies()
{
  EnsureBuiltinFeaturePlacementStrategies();
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
  const int maxScanY =
      ComputeMaxScanY(surfaceY, ctx.Settings.SeaLevel, ctx.Settings.MaxHeight);
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
  VoxelFeatureSink sink(ctx.World);
  const ObjectPlacementStats stats = PlaceObjectAt(
      ctx.World, ctx.Registry, *prefab, anchorWorldPos, false,
      ObjectWorldGenPlacementOptions{maxScanY, ctx.Settings.SeaLevel},
      [&sink](glm::ivec3 position, BlockId block)
      { sink.WriteVoxel(position, block); });
  if (stats.placedCount == 0)
  {
    return false;
  }
  ctx.AccumulateDirtyColumn(stats.minY, stats.maxY);
  return true;
}

bool TryPlaceVegetationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome,
                                const BiomeKey &biomeKey)
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
    if (!HillsVegetationAllowed(placementSurface.topSolidY,
                                ctx.Settings.SeaLevel, ctx.Settings.MaxHeight))
    {
      return false;
    }
  }
  return TryPlaceObjectPool(ctx, x, z, surfaceY, biome,
                            GetObjectFeatures(ctx).Vegetation,
                            ObjectFeaturePool::Vegetation, 5000, false, true,
                            ctx.Settings.Tuning.vegetationDensity, biomeKey);
}

bool TryPlaceGroundCoverFeatures(WorldGenContext &ctx, int x, int z,
                                 int surfaceY, BiomeId biome,
                                 bool skipIfTreeNearby,
                                 const BiomeKey &biomeKey)
{
  // A tree placed in this exact column is not a blanket reason to suppress
  // undergrowth. The plant/object placement checks below reject actual voxel
  // collisions while allowing cover in open space under the crown.
  if (skipIfTreeNearby && ctx.Settings.WorldGenProfileVersion < 2)
  {
    return false;
  }
  (void)skipIfTreeNearby;
  if (!UObjectFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableGroundCover)
  {
    return false;
  }
  return TryPlaceObjectPool(
      ctx, x, z, surfaceY, biome, GetObjectFeatures(ctx).GroundCover,
      ObjectFeaturePool::GroundCover, 5500, false, true,
      ctx.Settings.Tuning.vegetationDensity * 0.85f, biomeKey);
}

bool TryPlaceDecorationFeatures(WorldGenContext &ctx, int x, int z,
                                int surfaceY, BiomeId biome,
                                const BiomeKey &biomeKey)
{
  if (!UObjectFeatureConfigStorage::IsLoaded() ||
      !ctx.Settings.EnableDecoration)
  {
    return false;
  }
  return TryPlaceObjectPool(ctx, x, z, surfaceY, biome,
                            GetObjectFeatures(ctx).Decoration,
                            ObjectFeaturePool::Decoration, 6000, false, false,
                            ctx.Settings.Tuning.decorationDensity, biomeKey);
}

bool TryPlaceStructureFeatures(WorldGenContext &ctx, int x, int z, int surfaceY,
                               BiomeId biome, const BiomeKey &biomeKey)
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
                            ctx.Settings.Tuning.structureDensity, biomeKey);
}

namespace
{

class MossPatchCavePlacementStrategy final
    : public ICaveFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, const CaveFeatureRule &rule,
             glm::ivec3 floorPosition, BlockId featureBlock, uint32_t,
             glm::ivec3 chunkCoordinate,
             const std::function<bool(BlockId)> &isCaveRock) const override
  {
    const int radius = std::max(0, rule.Radius);
    bool placed = false;
    VoxelFeatureSink sink(ctx.World);
    for (int dz = -radius; dz <= radius; ++dz)
    {
      for (int dx = -radius; dx <= radius; ++dx)
      {
        if (dx * dx + dz * dz > radius * radius)
        {
          continue;
        }
        const glm::ivec3 targetFloor(floorPosition.x + dx, floorPosition.y,
                                     floorPosition.z + dz);
        if (UChunkManager::WorldToChunk(targetFloor) != chunkCoordinate ||
            !isCaveRock(ctx.World.GetBlock(targetFloor)) ||
            !ctx.World.IsAir(targetFloor + glm::ivec3(0, 1, 0)))
        {
          continue;
        }
        sink.WriteVoxel(targetFloor, featureBlock);
        placed = true;
      }
    }
    if (placed)
    {
      ctx.AccumulateDirtyColumn(floorPosition.y, floorPosition.y + 1);
    }
    return placed;
  }
};

class StalagmiteCavePlacementStrategy final
    : public ICaveFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, const CaveFeatureRule &rule,
             glm::ivec3 floorPosition, BlockId featureBlock, uint32_t hash,
             glm::ivec3, const std::function<bool(BlockId)> &) const override
  {
    const int maxHeight = std::clamp(rule.MaxHeight, 1, 64);
    const int requestedHeight =
        1 + static_cast<int>((hash >> 8U) % static_cast<uint32_t>(maxHeight));
    int height = 0;
    for (int dy = 1; dy <= requestedHeight; ++dy)
    {
      if (!ctx.World.IsAir(floorPosition + glm::ivec3(0, dy, 0)))
      {
        break;
      }
      height = dy;
    }
    if (height <= 0)
    {
      return false;
    }

    VoxelFeatureSink sink(ctx.World);
    for (int dy = 1; dy <= height; ++dy)
    {
      sink.WriteVoxel(floorPosition + glm::ivec3(0, dy, 0), featureBlock);
    }
    ctx.AccumulateDirtyColumn(floorPosition.y, floorPosition.y + height);
    return true;
  }
};

class AmethystClusterCavePlacementStrategy final
    : public ICaveFeaturePlacementStrategy
{
public:
  bool Place(WorldGenContext &ctx, const CaveFeatureRule &rule,
             glm::ivec3 floorPosition, BlockId featureBlock, uint32_t hash,
             glm::ivec3 chunkCoordinate,
             const std::function<bool(BlockId)> &) const override
  {
    const glm::ivec3 center = floorPosition + glm::ivec3(0, 1, 0);
    if (!ctx.World.IsAir(center) ||
        !ctx.World.IsAir(center + glm::ivec3(0, 1, 0)))
    {
      return false;
    }

    VoxelFeatureSink sink(ctx.World);
    sink.WriteVoxel(center, featureBlock);
    if (rule.Radius > 0 && (hash & 1U) != 0U)
    {
      const glm::ivec3 side = center + glm::ivec3(1, 0, 0);
      if (ctx.World.IsAir(side) &&
          UChunkManager::WorldToChunk(side) == chunkCoordinate)
      {
        sink.WriteVoxel(side, featureBlock);
      }
    }
    ctx.AccumulateDirtyColumn(floorPosition.y, floorPosition.y + 1);
    return true;
  }
};

void EnsureBuiltinCaveFeatureStrategies()
{
  static std::once_flag registered;
  std::call_once(
      registered,
      []
      {
        CaveFeaturePlacementStrategyRegistry::Register(
            CaveFeatureKind::MossPatch,
            std::make_shared<MossPatchCavePlacementStrategy>());
        CaveFeaturePlacementStrategyRegistry::Register(
            CaveFeatureKind::Stalagmite,
            std::make_shared<StalagmiteCavePlacementStrategy>());
        CaveFeaturePlacementStrategyRegistry::Register(
            CaveFeatureKind::AmethystCluster,
            std::make_shared<AmethystClusterCavePlacementStrategy>());
      });
}

} // namespace

bool TryPlaceCaveFeatures(
    WorldGenContext &ctx, int x, int z, int surfaceY,
    const std::function<BiomeId(int, int, int, int)> &biome_at_3d,
    const std::function<BiomeKey(int, int, int, int)> &biome_key_at_3d)
{
  if (!ctx.Settings.EnableCaves || !biome_at_3d ||
      surfaceY < ctx.Settings.MinHeight + 12)
  {
    return false;
  }
  const ObjectFeatureConfig &config = GetObjectFeatures(ctx);
  if (config.CaveFeatures.empty())
  {
    return false;
  }
  EnsureBuiltinCaveFeatureStrategies();
  const std::array<BlockId, 7> cave_rock = {
      ctx.Blocks.Stone,
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                "granite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                "diorite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                "andesite"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                "deepslate"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId, "tuff"),
      ResolvePackScatterBlockId(ctx.Registry, ctx.WorldgenOwnerPackId,
                                "rooted_dirt")};
  const auto is_cave_rock = [&cave_rock](BlockId id)
  {
    return id != BLOCK_AIR &&
           std::find(cave_rock.begin(), cave_rock.end(), id) != cave_rock.end();
  };
  const glm::ivec3 current_chunk =
      UChunkManager::WorldToChunk(glm::ivec3(x, 0, z));

  for (int floor_y = ctx.Settings.MinHeight + 5; floor_y < surfaceY - 4;
       ++floor_y)
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
    const BiomeKey cave_key = biome_key_at_3d
                                  ? biome_key_at_3d(x, floor_y + 1, z, surfaceY)
                                  : BiomeIdToString(cave_biome);
    const bool is_builtin_cave = cave_biome == BiomeId::LushCaves ||
                                 cave_biome == BiomeId::DripstoneCaves ||
                                 cave_biome == BiomeId::DeepDark;
    const BiomePackDefinition *cave_definition =
        cave_key.empty() ? nullptr
                         : UWorldGenPack::BiomeDefinitionFor(cave_key);
    if (!is_builtin_cave &&
        (!cave_definition || cave_definition->Domain != BiomeDomain::Cave))
    {
      continue;
    }
    for (const CaveFeatureRule &rule : config.CaveFeatures)
    {
      if ((!rule.BiomeKeyName.empty() && rule.BiomeKeyName != cave_key) ||
          (rule.BiomeKeyName.empty() && rule.Biome != cave_biome))
      {
        continue;
      }
      const char *featureName = "moss_patch";
      switch (rule.Kind)
      {
      case CaveFeatureKind::MossPatch:
        featureName = "moss_patch";
        break;
      case CaveFeatureKind::Stalagmite:
        featureName = "stalagmite";
        break;
      case CaveFeatureKind::AmethystCluster:
        featureName = "amethyst_cluster";
        break;
      }
      const float profileWeight =
          UWorldGenPack::FeatureWeightMultiplier(cave_key, featureName);
      if (profileWeight <= 0.0f)
      {
        continue;
      }
      const int effectiveChance =
          std::max(1, static_cast<int>(std::lround(
                          static_cast<float>(rule.Chance) / profileWeight)));
      uint32_t hash = ColumnHash(x, z, ctx.Settings.Seed + rule.SeedOffset);
      hash ^= static_cast<uint32_t>(floor_y) * 0x9e3779b9U;
      hash ^= hash >> 16U;
      if (hash % static_cast<uint32_t>(effectiveChance) != 0U)
      {
        continue;
      }

      const BlockId feature_block = ResolvePackScatterBlockId(
          ctx.Registry, ctx.WorldgenOwnerPackId, rule.BlockName);
      if (feature_block == BLOCK_AIR)
      {
        continue;
      }
      const auto strategy =
          CaveFeaturePlacementStrategyRegistry::Find(rule.Kind);
      if (strategy && strategy->Place(ctx, rule, floor_pos, feature_block, hash,
                                      current_chunk, is_cave_rock))
      {
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
