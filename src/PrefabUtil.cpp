#include "PrefabUtil.h"
#include <algorithm>

namespace cutum {

bool CanPlacePrefabAt(const BlockWorld& world, const Prefab& prefab, glm::ivec3 anchorWorldPos)
{
 for (const auto& voxel : prefab.voxels) {
  const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab.anchor;
  if (!world.IsAir(worldPos)) {
   return false;
  }
 }
 return !prefab.voxels.empty();
}

PrefabPlacementStats PlacePrefabAt(BlockWorld& world, const Prefab& prefab, glm::ivec3 anchorWorldPos,
    bool skipOccupied)
{
 PrefabPlacementStats stats;
 if (prefab.voxels.empty()) {
  return stats;
 }

 stats.minY = anchorWorldPos.y;
 stats.maxY = anchorWorldPos.y;

 for (const auto& voxel : prefab.voxels) {
  const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab.anchor;
  if (skipOccupied && !world.IsAir(worldPos)) {
   continue;
  }
  world.SetBlock(worldPos, voxel.id);
  stats.minY = std::min(stats.minY, worldPos.y);
  stats.maxY = std::max(stats.maxY, worldPos.y);
  ++stats.placedCount;
 }
 return stats;
}

} // namespace cutum
