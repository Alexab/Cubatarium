#include "WorldGen/Core/WorldGenContext.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include <iostream>
#include <unordered_set>

namespace cutum
{

namespace
{

BlockId ResolveSlotName(UBlockRegistry &registry,
                        const std::string &worldgenOwner,
                        const std::string &blockName)
{
  if (!worldgenOwner.empty())
  {
    const BlockId qualified = registry.GetIdByTypeName(
        MakeQualifiedBlockName(worldgenOwner, blockName));
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return registry.GetIdByTypeName(blockName);
}

BlockId ResolveSlot(UBlockRegistry &registry, const std::string &worldgenOwner,
                    const std::string &slotName,
                    std::unordered_set<std::string> &visited)
{
  if (!visited.insert(slotName).second)
  {
    return BLOCK_AIR;
  }

  const WorldGenSlotSpec *spec = UWorldGenRefs::GetSlot(slotName);
  if (spec)
  {
    for (const std::string &blockName : spec->BlockNames)
    {
      const BlockId id = ResolveSlotName(registry, worldgenOwner, blockName);
      if (id != BLOCK_AIR)
      {
        return id;
      }
    }
    if (!spec->FallbackSlot.empty())
    {
      return ResolveSlot(registry, worldgenOwner, spec->FallbackSlot, visited);
    }
    return BLOCK_AIR;
  }

  return ResolveSlotName(registry, worldgenOwner, slotName);
}

} // namespace

void WorldGenContext::ResolveBlockIds()
{
  const auto resolve = [this](const char *slotName, BlockId &out)
  {
    std::unordered_set<std::string> visited;
    out = ResolveSlot(Registry, WorldgenOwnerPackId, slotName, visited);
    if (out == BLOCK_AIR)
    {
      std::cerr << "WorldGen: missing block type for slot '" << slotName
                << "' (check worldgen_refs and active packs)" << std::endl;
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

  if (Settings.FillWater && Water == BLOCK_AIR)
  {
    std::cerr << "WorldGen: block type 'water' not loaded — fill_water will "
                 "have no effect"
              << std::endl;
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
