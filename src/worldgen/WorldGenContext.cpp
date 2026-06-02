#include "WorldGenContext.h"
#include "BlockWorld.h"
#include "BlockRegistry.h"
#include "ChunkManager.h"
#include "ChunkMeshCache.h"
#include <iostream>

namespace cutum {

void WorldGenContext::ResolveBlockIds()
{
 auto resolve = [this](const char* name, BlockId& out) {
  out = registry.GetIdByTypeName(name);
  if (out == BLOCK_AIR) {
   std::cerr << "WorldGen: missing block type '" << name << "', fallback stone/air" << std::endl;
  }
 };
 resolve("bedrock", bedrock);
 resolve("stone", stone);
 resolve("dirt", dirt);
 resolve("grass", grass);
 resolve("sand", sand);
 resolve("sandstone", sandstone);
 resolve("wood", wood);
 resolve("gravel", gravel);
 resolve("snow", snow);
 resolve("clay", clay);
 resolve("ice", ice);
 resolve("hellrock", hellrock);
 resolve("water", water);
 resolve("lava", lava);
 resolve("fire", fire);
 if (settings.fillWater && water == BLOCK_AIR) {
  std::cerr << "WorldGen: block type 'water' not loaded — fill_water will have no effect"
            << std::endl;
 }
 if (gravel == BLOCK_AIR) {
  gravel = stone;
 }
 if (snow == BLOCK_AIR) {
  snow = stone;
 }
 if (sand == BLOCK_AIR) {
  sand = sandstone != BLOCK_AIR ? sandstone : stone;
 }
 if (dirt == BLOCK_AIR) {
  dirt = stone;
 }
}

void WorldGenContext::MarkDirtyColumn(int worldX, int worldZ, int minY, int maxY) const
{
 if (!meshCache) {
  return;
 }
 std::unordered_set<glm::ivec3, IVec3Hash> dirtyChunks;
 for (int y = minY; y <= maxY; ++y) {
  dirtyChunks.insert(ChunkManager::WorldToChunk(glm::ivec3(worldX, y, worldZ)));
 }
 for (const glm::ivec3& coord : dirtyChunks) {
  meshCache->MarkDirty(coord);
 }
}

} // namespace cutum
