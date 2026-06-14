#include "WorldGen/Core/WorldGenContext.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Chunks/ChunkManager.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include <iostream>

namespace cutum
{

void WorldGenContext::ResolveBlockIds()
{
  auto resolve = [this](const char *name, BlockId &out)
  {
    out = Registry.GetIdByTypeName(name);
    if (out == BLOCK_AIR)
    {
      std::cerr << "WorldGen: missing block type '" << name
                << "', fallback stone/air" << std::endl;
    }
  };
  resolve("bedrock", Bedrock);
  resolve("stone", Stone);
  resolve("dirt", Dirt);
  resolve("grass", Grass);
  resolve("sand", Sand);
  resolve("sandstone", Sandstone);
  resolve("wood", Wood);
  resolve("gravel", Gravel);
  resolve("snow", Snow);
  resolve("clay", Clay);
  resolve("ice", Ice);
  resolve("hellrock", Hellrock);
  resolve("water", Water);
  resolve("lava", Lava);
  resolve("fire", Fire);
  if (Settings.fillWater && Water == BLOCK_AIR)
  {
    std::cerr << "WorldGen: block type 'water' not loaded — fill_water will "
                 "have no effect"
              << std::endl;
  }
  if (Gravel == BLOCK_AIR)
  {
    Gravel = Stone;
  }
  if (Snow == BLOCK_AIR)
  {
    Snow = Stone;
  }
  if (Sand == BLOCK_AIR)
  {
    Sand = Sandstone != BLOCK_AIR ? Sandstone : Stone;
  }
  if (Dirt == BLOCK_AIR)
  {
    Dirt = Stone;
  }
}

void WorldGenContext::MarkDirtyColumn(int world_x, int world_z, int min_y,
                                      int max_y) const
{
  if (!MeshCache)
  {
    return;
  }
  std::unordered_set<glm::ivec3, IVec3Hash> dirty_chunks;
  for (int y = min_y; y <= max_y; ++y)
  {
    dirty_chunks.insert(
        UChunkManager::WorldToChunk(glm::ivec3(world_x, y, world_z)));
  }
  for (const glm::ivec3 &coord : dirty_chunks)
  {
    MeshCache->MarkDirty(coord);
  }
}

} // namespace cutum
