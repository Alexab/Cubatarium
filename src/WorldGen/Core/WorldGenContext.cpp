#include "WorldGen/Core/WorldGenContext.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Chunks/ChunkManager.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include <algorithm>
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

WorldGenContext::WorldGenContext(UBlockWorld &world, UBlockRegistry &registry,
                                 ProceduralSettings settings,
                                 UPrefabLibrary *prefabs)
    : World(world), Registry(registry), Settings(std::move(settings)),
      Prefabs(prefabs)
{
}

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
  resolve("ore_coal", OreCoal);
  resolve("ore_iron", OreIron);

  if (Settings.FillWater && Water == BLOCK_AIR)
  {
    std::cerr << "WorldGen: block type 'water' not loaded — fill_water will "
                 "have no effect"
              << std::endl;
  }
}

void WorldGenContext::ResetColumnDirty(int world_x, int world_z)
{
  ColumnDirtyActive = true;
  ColumnDirtyWorldX = world_x;
  ColumnDirtyWorldZ = world_z;
  ColumnDirtyMinY = 0;
  ColumnDirtyMaxY = -1;
}

void WorldGenContext::AccumulateDirtyColumn(int min_y, int max_y)
{
  if (!ColumnDirtyActive)
  {
    return;
  }
  ColumnDirtyMinY = std::min(ColumnDirtyMinY, min_y);
  ColumnDirtyMaxY = std::max(ColumnDirtyMaxY, max_y);
}

void WorldGenContext::FlushColumnDirty()
{
  if (ColumnDirtyActive && ColumnDirtyMaxY >= ColumnDirtyMinY)
  {
    MarkDirtyColumn(ColumnDirtyWorldX, ColumnDirtyWorldZ, ColumnDirtyMinY,
                    ColumnDirtyMaxY);
  }
  ColumnDirtyActive = false;
}

void WorldGenContext::MarkDirtyColumn(int world_x, int world_z, int min_y,
                                      int max_y) const
{
  if (!OnColumnMeshDirty)
  {
    return;
  }
  OnColumnMeshDirty(world_x, world_z, min_y, max_y);
}

} // namespace cutum
