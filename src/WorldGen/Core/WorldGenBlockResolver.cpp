#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include <iostream>
#include <unordered_set>

namespace cutum
{

namespace
{

BlockId ResolveSlotName(UBlockRegistry &registry, const std::string &worldgen_owner,
                        const std::string &block_name)
{
  if (!worldgen_owner.empty())
  {
    const BlockId qualified = registry.GetPackBlockIdByTypeName(
        MakeQualifiedBlockName(worldgen_owner, block_name));
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return registry.GetPackBlockIdByTypeName(block_name);
}

BlockId ResolveSlot(UBlockRegistry &registry, const std::string &worldgen_owner,
                    const std::string &slot_name,
                    std::unordered_set<std::string> &visited)
{
  if (!visited.insert(slot_name).second)
  {
    return BLOCK_AIR;
  }

  const WorldGenSlotSpec *spec = UWorldGenRefs::GetSlot(slot_name);
  if (spec)
  {
    for (const std::string &block_name : spec->BlockNames)
    {
      const BlockId id = ResolveSlotName(registry, worldgen_owner, block_name);
      if (id != BLOCK_AIR)
      {
        return id;
      }
    }
    if (!spec->FallbackSlot.empty())
    {
      return ResolveSlot(registry, worldgen_owner, spec->FallbackSlot, visited);
    }
    return BLOCK_AIR;
  }

  return ResolveSlotName(registry, worldgen_owner, slot_name);
}

} // namespace

void WorldGenBlockResolver::Resolve(UBlockRegistry &registry,
                                    const std::string &worldgen_owner_pack_id)
{
  const auto resolve = [&](const char *slot_name, BlockId &out)
  {
    std::unordered_set<std::string> visited;
    out = ResolveSlot(registry, worldgen_owner_pack_id, slot_name, visited);
    if (out == BLOCK_AIR)
    {
      std::cerr << "WorldGen: missing block type for slot '" << slot_name
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
}

BlockId ResolvePackScatterBlockId(UBlockRegistry &registry,
                                  const std::string &worldgen_owner_pack_id,
                                  const std::string &block_name)
{
  if (block_name.empty())
  {
    return BLOCK_AIR;
  }
  if (!worldgen_owner_pack_id.empty())
  {
    const BlockId qualified = registry.GetPackBlockIdByTypeName(
        MakeQualifiedBlockName(worldgen_owner_pack_id, block_name));
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return registry.GetPackBlockIdByTypeName(block_name);
}

} // namespace cutum
