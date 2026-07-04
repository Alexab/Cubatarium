#include "World/Physics/FluidBlockResolver.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidPermeabilityUtil.h"

#include <array>

namespace cutum
{

namespace
{

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
  }
  return false;
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid && def->Physics.FluidMaxLevel >= 7;
  }
  return false;
}

bool IsFluidPermeableId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return IsFluidPermeableFromDefinition(id, definitions.GetById(id),
                                        IsLiquidId(definitions, id));
}

bool CellHasActiveFluid(const UBlockWorld &blockWorld,
                        const UBlockDefinitionStorage &definitions,
                        glm::ivec3 pos)
{
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsLiquidId(definitions, id))
  {
    return true;
  }
  if (IsFluidPermeableId(definitions, id))
  {
    return FluidCellHasActiveFluid(
        PackFluidCellState(blockWorld.GetFluidState(pos)));
  }
  return false;
}

BlockId ResolveWaterBlockIdImpl(const UBlockDefinitionStorage &definitions)
{
  for (const auto &entry : definitions.GetAll())
  {
    if (IsWaterKind(definitions, entry.first))
    {
      return entry.first;
    }
  }
  return BLOCK_AIR;
}

BlockId BlockIdFromFluidKindImpl(const UBlockDefinitionStorage &definitions,
                                 FluidKind kind)
{
  switch (kind)
  {
  case FluidKind::Water:
    return ResolveWaterBlockIdImpl(definitions);
  case FluidKind::Lava:
    for (const auto &entry : definitions.GetAll())
    {
      if (IsLiquidId(definitions, entry.first) &&
          !IsWaterKind(definitions, entry.first))
      {
        return entry.first;
      }
    }
    return BLOCK_AIR;
  default:
    return BLOCK_AIR;
  }
}

void ConsiderLiquidNeighbor(const UBlockDefinitionStorage &definitions,
                            BlockId id, BlockId &water_liquid,
                            BlockId &other_liquid)
{
  if (!IsLiquidId(definitions, id))
  {
    return;
  }
  if (IsWaterKind(definitions, id))
  {
    water_liquid = id;
    return;
  }
  if (other_liquid == BLOCK_AIR)
  {
    other_liquid = id;
  }
}

void ConsiderWaterloggedNeighbor(const UBlockWorld &blockWorld,
                                 const UBlockDefinitionStorage &definitions,
                                 glm::ivec3 neighbor_pos,
                                 BlockId &water_liquid, BlockId &other_liquid)
{
  const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
  if (!IsFluidPermeableId(definitions, neighbor_id) ||
      !CellHasActiveFluid(blockWorld, definitions, neighbor_pos))
  {
    return;
  }
  const FluidCellState neighbor_fluid = blockWorld.GetFluidState(neighbor_pos);
  if (neighbor_fluid.HasExplicitKind())
  {
    ConsiderLiquidNeighbor(
        definitions,
        BlockIdFromFluidKindImpl(definitions, neighbor_fluid.GetKind()),
        water_liquid, other_liquid);
    return;
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &inner_offset : kDirs)
  {
    ConsiderLiquidNeighbor(
        definitions, blockWorld.GetBlock(neighbor_pos + inner_offset),
        water_liquid, other_liquid);
  }
}

void ConsiderWaterloggedNeighborMesh(
    const IUChunkMeshReader &reader, const UBlockDefinitionStorage &definitions,
    glm::ivec3 neighbor_pos, BlockId &water_liquid, BlockId &other_liquid)
{
  const BlockId neighbor_id = reader.GetBlock(neighbor_pos);
  if (!IsFluidPermeableId(definitions, neighbor_id))
  {
    return;
  }
  const FluidCellState neighbor_fluid = reader.GetFluid(neighbor_pos);
  if (!FluidCellHasActiveFluid(PackFluidCellState(neighbor_fluid)))
  {
    return;
  }
  if (neighbor_fluid.HasExplicitKind())
  {
    ConsiderLiquidNeighbor(
        definitions,
        BlockIdFromFluidKindImpl(definitions, neighbor_fluid.GetKind()),
        water_liquid, other_liquid);
    return;
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &inner_offset : kDirs)
  {
    ConsiderLiquidNeighbor(definitions,
                           reader.GetBlock(neighbor_pos + inner_offset),
                           water_liquid, other_liquid);
  }
}

BlockId ResolveFluidKindImpl(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId block_id)
{
  if (IsLiquidId(definitions, block_id))
  {
    return block_id;
  }
  const FluidCellState fluid = blockWorld.GetFluidState(block_pos);
  if (fluid.HasExplicitKind())
  {
    return BlockIdFromFluidKindImpl(definitions, fluid.GetKind());
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  BlockId water_liquid = BLOCK_AIR;
  BlockId other_liquid = BLOCK_AIR;
  if (CellHasActiveFluid(blockWorld, definitions, block_pos))
  {
    for (const glm::ivec3 &offset : kDirs)
    {
      ConsiderLiquidNeighbor(definitions,
                             blockWorld.GetBlock(block_pos + offset),
                             water_liquid, other_liquid);
    }
  }
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    ConsiderLiquidNeighbor(definitions, blockWorld.GetBlock(neighbor_pos),
                           water_liquid, other_liquid);
    ConsiderWaterloggedNeighbor(blockWorld, definitions, neighbor_pos,
                                water_liquid, other_liquid);
  }
  if (water_liquid != BLOCK_AIR)
  {
    return water_liquid;
  }
  return other_liquid;
}

} // namespace

UFluidBlockResolver::UFluidBlockResolver(
    const UBlockDefinitionStorage &definitions)
    : Definitions(definitions)
{
}

BlockId UFluidBlockResolver::ResolveWaterBlockId(
    const UBlockDefinitionStorage &definitions)
{
  return ResolveWaterBlockIdImpl(definitions);
}

FluidKind UFluidBlockResolver::FluidKindFromBlockId(
    const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (IsWaterKind(definitions, id))
  {
    return FluidKind::Water;
  }
  if (IsLiquidId(definitions, id))
  {
    return FluidKind::Lava;
  }
  return FluidKind::None;
}

BlockId UFluidBlockResolver::BlockIdFromFluidKind(
    const UBlockDefinitionStorage &definitions, FluidKind kind)
{
  return BlockIdFromFluidKindImpl(definitions, kind);
}

BlockId UFluidBlockResolver::ResolveFluidBlockId(
    const UBlockWorld &block_world, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  const BlockId block_id = block_world.GetBlock(block_pos);
  return ResolveFluidKindImpl(block_world, definitions, block_pos, block_id);
}

BlockId UFluidBlockResolver::ResolveFluidKind(
    const UBlockWorld &block_world, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos, BlockId block_id)
{
  return ResolveFluidKindImpl(block_world, definitions, block_pos, block_id);
}

BlockId UFluidBlockResolver::ResolveFluidBlockIdForMesh(
    const IUChunkMeshReader &reader,
    const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos)
{
  const BlockId block_id = reader.GetBlock(block_pos);
  if (IsLiquidId(definitions, block_id))
  {
    return block_id;
  }
  const FluidCellState fluid = reader.GetFluid(block_pos);
  if (fluid.HasExplicitKind())
  {
    return BlockIdFromFluidKindImpl(definitions, fluid.GetKind());
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  BlockId water_liquid = BLOCK_AIR;
  BlockId other_liquid = BLOCK_AIR;
  if (FluidCellHasActiveFluid(PackFluidCellState(fluid)))
  {
    for (const glm::ivec3 &offset : kDirs)
    {
      ConsiderLiquidNeighbor(definitions,
                             reader.GetBlock(block_pos + offset), water_liquid,
                             other_liquid);
    }
  }
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    ConsiderLiquidNeighbor(definitions, reader.GetBlock(neighbor_pos),
                           water_liquid, other_liquid);
    ConsiderWaterloggedNeighborMesh(reader, definitions, neighbor_pos,
                                    water_liquid, other_liquid);
  }
  if (water_liquid != BLOCK_AIR)
  {
    return water_liquid;
  }
  return other_liquid;
}

BlockId UFluidBlockResolver::ResolveFluidBlockId(const UBlockWorld &block_world,
                                                 glm::ivec3 block_pos) const
{
  return ResolveFluidBlockId(block_world, Definitions, block_pos);
}

BlockId UFluidBlockResolver::ResolveFluidBlockIdForMesh(
    const IUChunkMeshReader &reader, glm::ivec3 block_pos) const
{
  return ResolveFluidBlockIdForMesh(reader, Definitions, block_pos);
}

FluidKind UFluidBlockResolver::ResolveFluidKind(const UBlockWorld &block_world,
                                                glm::ivec3 block_pos,
                                                BlockId block_id) const
{
  const BlockId resolved =
      ResolveFluidKind(block_world, Definitions, block_pos, block_id);
  return FluidKindFromBlockId(Definitions, resolved);
}

} // namespace cutum
