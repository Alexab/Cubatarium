#include "World/Physics/MaterialReactionRules.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/MaterialReactionRulesRegistry.h"

namespace cutum
{

namespace
{

static constexpr glm::ivec3 kNeighborOffsets[6] = {
    glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
    glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};

bool TryWaterMeetsLava(UBlockWorld &block_world, const UBlockRegistry &registry,
                       glm::ivec3 contact_pos, MaterialReactionResult &out)
{
  if (!block_world.IsAir(contact_pos))
  {
    return false;
  }
  bool has_water = false;
  bool has_lava = false;
  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    const BlockId id = block_world.GetBlock(contact_pos + offset);
    if (id == BLOCK_AIR)
    {
      continue;
    }
    if (registry.IsLiquid(id))
    {
      if (const UBlockDefinitionStorage *definitions = registry.GetDefinitions())
      {
        if (const BlockDefinition *def = definitions->GetById(id))
        {
          if (def->Physics.FluidMaxLevel >= 7)
          {
            has_water = true;
          }
          else
          {
            has_lava = true;
          }
        }
      }
    }
  }
  if (!has_water || !has_lava)
  {
    return false;
  }
  const BlockId stone_id =
      registry.GetIdByTypeName(MaterialReactionRegistry::StoneTypeName());
  if (stone_id == BLOCK_AIR)
  {
    return false;
  }
  out.Applied = true;
  out.BlockPos = contact_pos;
  out.NewBlock = stone_id;
  return true;
}

bool TryFireSpread(UBlockWorld &block_world, const UBlockRegistry &registry,
                   glm::ivec3 fire_pos, glm::ivec3 target_pos,
                   MaterialReactionResult &out)
{
  const BlockId fire_id = block_world.GetBlock(fire_pos);
  if (!registry.IsFireBlock(fire_id))
  {
    return false;
  }
  const BlockId target_id = block_world.GetBlock(target_pos);
  if (target_id == BLOCK_AIR || !registry.IsFlammable(target_id))
  {
    return false;
  }
  const BlockId spread_fire_id =
      registry.GetIdByTypeName(MaterialReactionRegistry::FireTypeName());
  if (spread_fire_id == BLOCK_AIR)
  {
    return false;
  }
  out.Applied = true;
  out.BlockPos = target_pos;
  out.NewBlock = spread_fire_id;
  return true;
}

} // namespace

std::vector<MaterialReactionResult>
UMaterialReactionRules::EvaluateNeighbors(UBlockWorld &block_world,
                                            const UBlockRegistry &registry,
                                            glm::ivec3 changed_pos) const
{
  std::vector<MaterialReactionResult> results;
  if (ShadowMode)
  {
    return results;
  }

  MaterialReactionResult water_lava;
  if (TryWaterMeetsLava(block_world, registry, changed_pos, water_lava))
  {
    block_world.SetBlock(water_lava.BlockPos, water_lava.NewBlock);
    results.push_back(water_lava);
  }

  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    const glm::ivec3 neighbor = changed_pos + offset;
    MaterialReactionResult spread;
    if (TryFireSpread(block_world, registry, neighbor, changed_pos, spread))
    {
      block_world.SetBlock(spread.BlockPos, spread.NewBlock);
      results.push_back(spread);
      continue;
    }
    if (TryFireSpread(block_world, registry, changed_pos, neighbor, spread))
    {
      block_world.SetBlock(spread.BlockPos, spread.NewBlock);
      results.push_back(spread);
    }
  }

  return results;
}

} // namespace cutum
