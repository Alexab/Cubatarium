#include "PrefabFeaturePlacer.h"
#include "PrefabUtil.h"
#include "Prefab.h"
#include "BlockWorld.h"
#include "ChunkManager.h"
#include <cstdint>

namespace cutum {

namespace {

uint32_t FeatureHash(int x, int z, uint32_t seed)
{
 return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ seed;
}

} // namespace

bool CanPlacePrefabAt(const WorldGenContext& ctx, const std::string& prefabName, glm::ivec3 anchorWorldPos)
{
 if (!ctx.Prefabs) {
  return false;
 }
 const Prefab* prefab = ctx.Prefabs->Get(prefabName);
 if (!prefab) {
  return false;
 }
 return CanPlacePrefabAt(ctx.World, *prefab, anchorWorldPos);
}

bool PlacePrefabAt(WorldGenContext& ctx, const std::string& prefabName, glm::ivec3 anchorWorldPos)
{
 if (!ctx.Prefabs) {
  return false;
 }
 const Prefab* prefab = ctx.Prefabs->Get(prefabName);
 if (!prefab || !CanPlacePrefabAt(ctx, prefabName, anchorWorldPos)) {
  return false;
 }
 const PrefabPlacementStats stats = PlacePrefabAt(ctx.World, *prefab, anchorWorldPos, false);
 if (stats.placedCount == 0) {
  return false;
 }
 ctx.MarkDirtyColumn(anchorWorldPos.x, anchorWorldPos.z, stats.minY, stats.maxY);
 return true;
}

bool TryPlaceTree(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome, const FeatureParams& params)
{
 if (!ctx.Settings.enableTrees || !ctx.Prefabs) {
  return false;
 }
 if (biome != BiomeId::Forest && biome != BiomeId::Plains) {
  return false;
 }

 const glm::ivec3 anchor(x, surfaceY + 1, z);
 const uint32_t seed = ctx.Settings.seed;

 if (biome == BiomeId::Forest) {
  if (FeatureHash(x, z, seed + params.treeLargeSeedOffset) %
          static_cast<uint32_t>(params.treeLargeSpacingModForest) == 0) {
   return PlacePrefabAt(ctx, params.treeLargePrefabName, anchor);
  }
  if (FeatureHash(x, z, seed + params.treeSeedOffset) %
          static_cast<uint32_t>(params.treeSmallSpacingModForest) == 0) {
   return PlacePrefabAt(ctx, params.treeSmallPrefabName, anchor);
  }
  return false;
 }

 if (FeatureHash(x, z, seed + params.treeSeedOffset) %
         static_cast<uint32_t>(params.treeSmallSpacingModPlains) != 0) {
  return false;
 }
 if (FeatureHash(x, z, seed + params.treeSeedOffset + 7) % 5 != 0) {
  return false;
 }
 return PlacePrefabAt(ctx, params.treeSmallPrefabName, anchor);
}

bool TryPlaceLavaPool(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome)
{
 if (!ctx.Settings.fillLava || ctx.Lava == BLOCK_AIR || biome != BiomeId::Hills) {
  return false;
 }
 const uint32_t seed = ctx.Settings.seed;
 if (FeatureHash(x, z, seed + 9001) % 400 != 0) {
  return false;
 }
 for (int dx = -1; dx <= 1; ++dx) {
  for (int dz = -1; dz <= 1; ++dz) {
   const glm::ivec3 pos(x + dx, surfaceY + 1, z + dz);
   const BlockId below = ctx.World.GetBlock(glm::ivec3(x + dx, surfaceY, z + dz));
   if (below != ctx.Stone && below != ctx.Gravel) {
    return false;
   }
   if (!ctx.World.IsAir(pos)) {
    return false;
   }
   ctx.World.SetBlock(pos, ctx.Lava);
  }
 }
 ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
 return true;
}

bool TryPlaceFirePatch(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome, BlockId grassId)
{
 if (!ctx.Settings.fillFire || ctx.Fire == BLOCK_AIR) {
  return false;
 }
 (void)biome;
 const BlockId surface = ctx.World.GetBlock(glm::ivec3(x, surfaceY, z));
 if (surface != grassId) {
  return false;
 }
 const glm::ivec3 firePos(x, surfaceY + 1, z);
 if (!ctx.World.IsAir(firePos)) {
  return false;
 }
 const uint32_t seed = ctx.Settings.seed;
 if (FeatureHash(x, z, seed + 12007) % 512 != 0) {
  return false;
 }
 ctx.World.SetBlock(firePos, ctx.Fire);
 ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
 return true;
}

} // namespace cutum
