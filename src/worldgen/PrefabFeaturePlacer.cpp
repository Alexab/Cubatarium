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
 if (!ctx.prefabs) {
  return false;
 }
 const Prefab* prefab = ctx.prefabs->Get(prefabName);
 if (!prefab) {
  return false;
 }
 return CanPlacePrefabAt(ctx.world, *prefab, anchorWorldPos);
}

bool PlacePrefabAt(WorldGenContext& ctx, const std::string& prefabName, glm::ivec3 anchorWorldPos)
{
 if (!ctx.prefabs) {
  return false;
 }
 const Prefab* prefab = ctx.prefabs->Get(prefabName);
 if (!prefab || !CanPlacePrefabAt(ctx, prefabName, anchorWorldPos)) {
  return false;
 }
 const PrefabPlacementStats stats = PlacePrefabAt(ctx.world, *prefab, anchorWorldPos, false);
 if (stats.placedCount == 0) {
  return false;
 }
 ctx.MarkDirtyColumn(anchorWorldPos.x, anchorWorldPos.z, stats.minY, stats.maxY);
 return true;
}

bool TryPlaceTree(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome, const FeatureParams& params)
{
 if (!ctx.settings.enableTrees || !ctx.prefabs) {
  return false;
 }
 if (biome != BiomeId::Forest && biome != BiomeId::Plains) {
  return false;
 }

 const glm::ivec3 anchor(x, surfaceY + 1, z);
 const uint32_t seed = ctx.settings.seed;

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
 if (!ctx.settings.fillLava || ctx.lava == BLOCK_AIR || biome != BiomeId::Hills) {
  return false;
 }
 const uint32_t seed = ctx.settings.seed;
 if (FeatureHash(x, z, seed + 9001) % 400 != 0) {
  return false;
 }
 for (int dx = -1; dx <= 1; ++dx) {
  for (int dz = -1; dz <= 1; ++dz) {
   const glm::ivec3 pos(x + dx, surfaceY + 1, z + dz);
   const BlockId below = ctx.world.GetBlock(glm::ivec3(x + dx, surfaceY, z + dz));
   if (below != ctx.stone && below != ctx.gravel) {
    return false;
   }
   if (!ctx.world.IsAir(pos)) {
    return false;
   }
   ctx.world.SetBlock(pos, ctx.lava);
  }
 }
 ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
 return true;
}

bool TryPlaceFirePatch(WorldGenContext& ctx, int x, int z, int surfaceY, BiomeId biome, BlockId grassId)
{
 if (!ctx.settings.fillFire || ctx.fire == BLOCK_AIR) {
  return false;
 }
 (void)biome;
 const BlockId surface = ctx.world.GetBlock(glm::ivec3(x, surfaceY, z));
 if (surface != grassId) {
  return false;
 }
 const glm::ivec3 firePos(x, surfaceY + 1, z);
 if (!ctx.world.IsAir(firePos)) {
  return false;
 }
 const uint32_t seed = ctx.settings.seed;
 if (FeatureHash(x, z, seed + 12007) % 512 != 0) {
  return false;
 }
 ctx.world.SetBlock(firePos, ctx.fire);
 ctx.MarkDirtyColumn(x, z, surfaceY, surfaceY + 2);
 return true;
}

} // namespace cutum
