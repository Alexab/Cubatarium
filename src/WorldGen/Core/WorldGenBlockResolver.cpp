#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "Core/Memory/GenerationString.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include <iostream>

namespace cutum
{

namespace
{

BlockId ResolveSlotName(UBlockRegistry &registry, const std::string &worldgen_owner,
                        const std::string &block_name)
{
  if (!worldgen_owner.empty())
  {
    const UGenerationString name({worldgen_owner,kQualifiedBlockSeparator,block_name});
    const BlockId qualified = registry.GetPackBlockIdByTypeName(name.Get());
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return registry.GetPackBlockIdByTypeName(block_name);
}

BlockId ResolveSlot(UBlockRegistry &registry, const std::string &worldgen_owner,
                    const std::string &slot_name,
                    const WorldGenRefsCatalog &refs)
{
  const auto find=[&](const std::string &name)->const WorldGenSlotSpec * {
    const auto it=refs.find(name);return it==refs.end() ? nullptr : &it->second;
  };
  const auto next=[&](const WorldGenSlotSpec *spec)->const WorldGenSlotSpec * {
    return spec && !spec->FallbackSlot.empty() ? find(spec->FallbackSlot) : nullptr;
  };
  const auto resolve_names=[&](const WorldGenSlotSpec &spec) {
    for(const auto &block:spec.BlockNames) {
      const auto id=ResolveSlotName(registry,worldgen_owner,block);
      if(id!=BLOCK_AIR) return id;
    }
    return BLOCK_AIR;
  };
  const auto *first=find(slot_name);
  if(!first) return ResolveSlotName(registry,worldgen_owner,slot_name);
  const auto direct=resolve_names(*first);
  if(direct!=BLOCK_AIR || first->FallbackSlot.empty()) return direct;
  // Find an exact reachable-cycle limit without copied names, a visited set,
  // or recursion. Unrelated catalog entries cannot prolong a short cycle.
  const auto *slow=next(first),*fast=next(next(first));
  while(slow && fast && slow!=fast) {slow=next(slow);fast=next(next(fast));}
  size_t limit=refs.size();
  if(slow && fast) {
    size_t prefix=0,cycle=1;slow=first;
    while(slow!=fast) {slow=next(slow);fast=next(fast);++prefix;}
    for(fast=next(slow);fast!=slow;fast=next(fast)) ++cycle;
    limit=prefix+cycle;
  }
  const std::string *name=&first->FallbackSlot;
  for(size_t resolved=1;;++resolved) {
    const auto *spec=find(*name);
    if(!spec) return ResolveSlotName(registry,worldgen_owner,*name);
    if(resolved==limit) return BLOCK_AIR;
    const auto id=resolve_names(*spec);
    if(id!=BLOCK_AIR) return id;
    if (!spec->FallbackSlot.empty())
    {
      name=&spec->FallbackSlot;
      continue;
    }
    return BLOCK_AIR;
  }

}

} // namespace

void WorldGenBlockResolver::Resolve(UBlockRegistry &registry,
                                    const std::string &worldgen_owner_pack_id)
{
  const auto *pin=GetPinnedWorldGenContent();
  const auto refs=pin && pin->Refs ? pin->Refs : UWorldGenRefs::GetSnapshot();
  const auto resolve = [&](const char *slot_name, BlockId &out)
  {
    out = refs ? ResolveSlot(registry,worldgen_owner_pack_id,slot_name,*refs)
               : ResolveSlotName(registry,worldgen_owner_pack_id,slot_name);
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
    const UGenerationString name({worldgen_owner_pack_id,kQualifiedBlockSeparator,block_name});
    const BlockId qualified = registry.GetPackBlockIdByTypeName(name.Get());
    if (qualified != BLOCK_AIR)
    {
      return qualified;
    }
  }
  return registry.GetPackBlockIdByTypeName(block_name);
}

} // namespace cutum
