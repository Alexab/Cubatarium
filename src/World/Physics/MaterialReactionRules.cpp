#include "World/Physics/MaterialReactionRules.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidKindPresetUtil.h"
#include "World/Physics/MaterialReactionRulesRegistry.h"

namespace cutum
{

namespace
{

static constexpr glm::ivec3 kNeighborOffsets[6] = {
    glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
    glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};

bool ClassifyLiquid(const UBlockRegistry &registry, BlockId id, bool &is_water,
                    bool &is_lava)
{
  is_water = false;
  is_lava = false;
  if (id == BLOCK_AIR || !registry.IsLiquid(id))
  {
    return false;
  }
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (!definitions)
  {
    return false;
  }
  const BlockDefinition *def = definitions->GetById(id);
  if (!def)
  {
    return false;
  }
  if (IsWaterFluidDefinition(def))
  {
    is_water = true;
  }
  else if (FluidKindFromDefinition(def) == FluidKind::Lava)
  {
    is_lava = true;
  }
  return is_water || is_lava;
}

/// Air cell between water and lava → stone (classic contact).
bool TryWaterMeetsLavaAtAir(UBlockWorld &block_world,
                            const UBlockRegistry &registry,
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
    bool w = false;
    bool l = false;
    if (ClassifyLiquid(registry, block_world.GetBlock(contact_pos + offset), w,
                       l))
    {
      has_water = has_water || w;
      has_lava = has_lava || l;
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

/// Direct water|lava adjacency: solidify the lava cell (stops dual-front spread).
bool TryWaterTouchesLava(UBlockWorld &block_world, const UBlockRegistry &registry,
                         glm::ivec3 water_pos, glm::ivec3 lava_pos,
                         MaterialReactionResult &out)
{
  bool water_ok = false;
  bool lava_ok = false;
  bool w = false;
  bool l = false;
  if (ClassifyLiquid(registry, block_world.GetBlock(water_pos), w, l) && w)
  {
    water_ok = true;
  }
  if (ClassifyLiquid(registry, block_world.GetBlock(lava_pos), w, l) && l)
  {
    lava_ok = true;
  }
  if (!water_ok || !lava_ok)
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
  out.BlockPos = lava_pos;
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

  // Old path only tested changed_pos as air — ProcessFluidChange passes fluid
  // cells, so water+lava never solidified and dual fronts kept spreading.
  {
    MaterialReactionResult air_at_changed;
    if (TryWaterMeetsLavaAtAir(block_world, registry, changed_pos,
                               air_at_changed))
    {
      block_world.SetBlock(air_at_changed.BlockPos, air_at_changed.NewBlock);
      block_world.ClearFluidState(air_at_changed.BlockPos);
      results.push_back(air_at_changed);
    }
  }
  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    const glm::ivec3 neighbor = changed_pos + offset;
    MaterialReactionResult air_contact;
    if (TryWaterMeetsLavaAtAir(block_world, registry, neighbor, air_contact))
    {
      block_world.SetBlock(air_contact.BlockPos, air_contact.NewBlock);
      block_world.ClearFluidState(air_contact.BlockPos);
      results.push_back(air_contact);
    }
    MaterialReactionResult touch_a;
    if (TryWaterTouchesLava(block_world, registry, changed_pos, neighbor,
                            touch_a))
    {
      block_world.SetBlock(touch_a.BlockPos, touch_a.NewBlock);
      block_world.ClearFluidState(touch_a.BlockPos);
      results.push_back(touch_a);
    }
    MaterialReactionResult touch_b;
    if (TryWaterTouchesLava(block_world, registry, neighbor, changed_pos,
                            touch_b))
    {
      block_world.SetBlock(touch_b.BlockPos, touch_b.NewBlock);
      block_world.ClearFluidState(touch_b.BlockPos);
      results.push_back(touch_b);
    }
  }

  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    const glm::ivec3 neighbor = changed_pos + offset;
    MaterialReactionResult spread;
    if (TryFireSpread(block_world, registry, neighbor, changed_pos, spread))
    {
      block_world.SetBlock(spread.BlockPos, spread.NewBlock);
      results.push_back(spread);
    }
  }

  return results;
}

} // namespace cutum
