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

} // namespace cutum
