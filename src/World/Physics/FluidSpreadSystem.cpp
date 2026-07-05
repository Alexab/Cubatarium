#include "World/Physics/FluidSpreadSystem.h"

#include "World/Physics/FluidBlockResolver.h"
#include "World/Physics/FluidFillPolicy.h"
#include "World/Physics/FluidFloodService.h"
#include "World/Physics/FluidTransformSim.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"

#include <algorithm>
#include <vector>

namespace cutum
{

bool UFluidSpreadSystem::CellTouchesWet(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return UFluidFloodService::CellTouchesWet(blockWorld, definitions, pos);
}

BlockId UFluidSpreadSystem::ResolveFloodFluidId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, const FluidFloodOptions &options)
{
  return UFluidFloodService::ResolveFloodFluidId(blockWorld, definitions, pos,
                                                 options);
}

int UFluidSpreadSystem::FloodWetPocketsInBox(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 box_min, glm::ivec3 box_max, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return UFluidFloodService::FloodWetPocketsInBox(
      blockWorld, definitions, box_min, box_max, options, out_changed);
}

int UFluidSpreadSystem::FloodWetPocketsLocal(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 center, int radius, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return UFluidFloodService::FloodWetPocketsLocal(
      blockWorld, definitions, center, radius, options, out_changed);
}

int UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 break_pos, const FluidFloodOptions &options,
    std::vector<glm::ivec3> *out_changed)
{
  return UFluidFloodService::FloodBreakSiteFromWetNeighbors(
      blockWorld, definitions, break_pos, options, out_changed);
}

bool UFluidSpreadSystem::CanReceiveFluid(const UBlockWorld &blockWorld,
                                         const UBlockRegistry &registry,
                                         glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return blockWorld.IsAir(pos);
  }
  return UFluidFillPolicy::CanReceiveFluid(blockWorld, *definitions, pos);
}

bool UFluidSpreadSystem::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockRegistry &registry,
    glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return blockWorld.IsAir(pos);
  }
  return UFluidFillPolicy::ShouldReplaceBlockWithFluid(blockWorld, *definitions,
                                                       pos);
}

bool UFluidSpreadSystem::CanReceiveFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions, pos);
}

bool UFluidSpreadSystem::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return UFluidFillPolicy::ShouldReplaceBlockWithFluid(blockWorld, definitions,
                                                       pos);
}

bool UFluidSpreadSystem::ShouldProcessFluidTick(uint64_t physics_tick,
                                                glm::ivec3 block_pos,
                                                int spread_period)
{
  const int period = std::max(1, spread_period);
  const uint32_t x = static_cast<uint32_t>(block_pos.x);
  const uint32_t y = static_cast<uint32_t>(block_pos.y);
  const uint32_t z = static_cast<uint32_t>(block_pos.z);
  const uint32_t phase = (x * 73856093u ^ y * 19349663u ^ z * 83492791u) %
                         static_cast<uint32_t>(period);
  return (physics_tick + phase) % static_cast<uint32_t>(period) == 0;
}

int UFluidSpreadSystem::SpreadPeriodForCell(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  const BlockId block_id = blockWorld.GetBlock(block_pos);
  if (const BlockDefinition *def = definitions.GetById(block_id))
  {
    if (def->Physics.IsLiquid)
    {
      return std::max(1, def->Physics.FluidSpreadPeriodTicks);
    }
  }
  const BlockId fluid_id =
      UFluidBlockResolver::ResolveFluidBlockId(blockWorld, definitions, block_pos);
  if (fluid_id != BLOCK_AIR)
  {
    if (const BlockDefinition *fluid_def = definitions.GetById(fluid_id))
    {
      return std::max(1, fluid_def->Physics.FluidSpreadPeriodTicks);
    }
  }
  return 5;
}

bool UFluidSpreadSystem::HasSpreadTarget(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  return UFluidTransformSim::HasSpreadTarget(blockWorld, definitions, block_pos);
}

FluidSpreadStats UFluidSpreadSystem::Tick(UWorld &world, glm::ivec3 block_pos)
{
  const UBlockDefinitionStorage *definitions =
      world.GetBlockRegistry().GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(world.GetBlockWorld(), *definitions,
                   world.GetPhysicsTickCounter(), block_pos,
                   world.GetProceduralSettings().FillWater
                       ? world.GetProceduralSettings().SeaLevel
                       : -1);
}

FluidSpreadStats UFluidSpreadSystem::TickBlock(UBlockWorld &blockWorld,
                                               const UBlockRegistry &registry,
                                               uint64_t physics_tick,
                                               glm::ivec3 block_pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(blockWorld, *definitions, physics_tick, block_pos, -1);
}

FluidSpreadStats
UFluidSpreadSystem::TickBlock(UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              uint64_t physics_tick, glm::ivec3 block_pos,
                              int sea_level)
{
  return UFluidTransformSim::TickBlock(blockWorld, definitions, physics_tick,
                                       block_pos, sea_level, ShadowMode);
}

BlockId UFluidSpreadSystem::ResolveWaterBlockId(
    const UBlockDefinitionStorage &definitions)
{
  return UFluidBlockResolver::ResolveWaterBlockId(definitions);
}

BlockId UFluidSpreadSystem::ResolveFluidKind(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos, BlockId block_id)
{
  return UFluidBlockResolver::ResolveFluidKind(blockWorld, definitions,
                                                block_pos, block_id);
}

FluidKind UFluidSpreadSystem::FluidKindFromBlockId(
    const UBlockDefinitionStorage &definitions, BlockId id)
{
  return UFluidBlockResolver::FluidKindFromBlockId(definitions, id);
}

BlockId UFluidSpreadSystem::BlockIdFromFluidKind(
    const UBlockDefinitionStorage &definitions, FluidKind kind)
{
  return UFluidBlockResolver::BlockIdFromFluidKind(definitions, kind);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  return UFluidBlockResolver::ResolveFluidBlockId(blockWorld, definitions,
                                                  block_pos);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockIdForMesh(
    const IUChunkMeshReader &reader,
    const UBlockDefinitionStorage &definitions, glm::ivec3 block_pos)
{
  return UFluidBlockResolver::ResolveFluidBlockIdForMesh(reader, definitions,
                                                         block_pos);
}

void UFluidSpreadSystem::ApplyFluidFill(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, BlockId fluid_id, FluidCellState state)
{
  UFluidFillPolicy::ApplyFluidFill(blockWorld, definitions, pos, fluid_id,
                                   state);
}

} // namespace cutum
