#include "World/Physics/FluidSpreadSystem.h"

#include "World/Physics/FluidTransformSim.h"
#include "World/Physics/FluidFillPolicy.h"
#include "World/Physics/FluidFloodService.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/IUChunkMeshReader.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidPermeabilityUtil.h"
#include "World/Physics/FluidTuning.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <algorithm>
#include <array>
#include <vector>

namespace cutum
{

namespace
{

enum class NeighborAxis
{
  Upper,
  Lower,
  Horizontal,
};

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
  }
  return false;
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id);

BlockId ResolveWaterBlockIdImpl(const UBlockDefinitionStorage &definitions);

BlockId ResolveFluidBlockIdImpl(const UBlockWorld &blockWorld,
                                const UBlockDefinitionStorage &definitions,
                                glm::ivec3 block_pos);

BlockId ResolveFluidKindImpl(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId block_id);

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

FluidKind FluidKindFromBlockIdImpl(const UBlockDefinitionStorage &definitions,
                                   BlockId id)
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

FluidCellState EnsureFluidKind(const UBlockDefinitionStorage &definitions,
                               BlockId fluid_id, FluidCellState state)
{
  if (state.HasExplicitKind())
  {
    return state;
  }
  const FluidKind kind = FluidKindFromBlockIdImpl(definitions, fluid_id);
  if (kind == FluidKind::None)
  {
    return state;
  }
  return state.WithKind(kind);
}

bool IsLiquidRenewable(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.LiquidRenewable;
  }
  return false;
}

int GetSpreadPeriod(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, def->Physics.FluidSpreadPeriodTicks);
  }
  return 5;
}

int GetFluidMaxLevel(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, def->Physics.FluidMaxLevel);
  }
  return static_cast<int>(FLUID_LEVEL_MAX);
}

int GetLiquidViscosity(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1, static_cast<int>(def->Physics.LiquidViscosity));
  }
  return 1;
}

int GetLiquidRange(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return std::min(static_cast<int>(FLUID_LEVEL_MAX) + 1,
                  GetFluidMaxLevel(definitions, id) + 1);
}

int MinSurviveLevel(const UBlockDefinitionStorage &definitions, BlockId id)
{
  const int max_level = GetFluidMaxLevel(definitions, id);
  const int min_level = static_cast<int>(FLUID_LEVEL_MAX) + 1 -
                        GetLiquidRange(definitions, id);
  if (min_level > max_level)
  {
    return 1;
  }
  return std::max(0, min_level);
}

FluidCellState EffectiveFluidState(const UBlockWorld &blockWorld,
                                   glm::ivec3 block_pos)
{
  FluidCellState state = blockWorld.GetFluidState(block_pos);
  if (state.Level == 0 && state.Falling == 0)
  {
    const uint8_t packed = PackFluidCellState(state);
    if (packed == 0)
    {
      return FluidCellState::Source();
    }
  }
  return state;
}

int LevelFromState(const FluidCellState &state)
{
  if (state.IsSource())
  {
    return 0;
  }
  return static_cast<int>(state.Level);
}

void RecordChange(FluidSpreadStats &stats, glm::ivec3 block_pos,
                  glm::ivec3 neighbor_pos, BlockId fluid_id,
                  FluidCellState new_state, bool removed_fluid,
                  const char *reason)
{
  ++stats.Applied;
  stats.Changes.push_back(
      {block_pos, neighbor_pos, fluid_id, new_state, removed_fluid});
  ULiquidDebugTrace::Instance().Record(block_pos, neighbor_pos, reason);
}

int MaxLevelFromNeighbor(const FluidCellState &neighbor_state,
                         NeighborAxis axis, int current_max)
{
  const int nb_level = LevelFromState(neighbor_state);
  switch (axis)
  {
  case NeighborAxis::Upper:
    if (neighbor_state.IsSource())
    {
      return std::max(current_max, 1);
    }
    if (nb_level > 0)
    {
      const int boosted = std::min(static_cast<int>(FLUID_LEVEL_MAX),
                                   nb_level + FluidTuning::WaterDropBoost);
      return std::max(current_max, boosted);
    }
    break;
  case NeighborAxis::Lower:
    break;
  case NeighborAxis::Horizontal:
    if (neighbor_state.Falling != 0)
    {
      break;
    }
    if (neighbor_state.IsSource())
    {
      return std::max(current_max, 1);
    }
    if (nb_level > 0)
    {
      return std::max(current_max, nb_level + 1);
    }
    break;
  }
  return current_max;
}

bool HorizontalSpreadAllowed(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos)
{
  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  return below.y < 0 ||
         !UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions, below);
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

bool HasOutboundSpreadTarget(const UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 block_pos, BlockId fluid_id)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    if (!UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions,
                                             neighbor_pos))
    {
      continue;
    }
    const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
    if (IsLiquidId(definitions, neighbor_id))
    {
      if (ResolveFluidKindImpl(blockWorld, definitions, neighbor_pos,
                               neighbor_id) == fluid_id)
      {
        return true;
      }
      continue;
    }
    if (IsFluidPermeableId(definitions, neighbor_id) &&
        CellHasActiveFluid(blockWorld, definitions, neighbor_pos) &&
        ResolveFluidBlockIdImpl(blockWorld, definitions, neighbor_pos) ==
            fluid_id)
    {
      continue;
    }
    return true;
  }
  return false;
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

BlockId ResolveFluidKind(const UBlockWorld &blockWorld,
                         const UBlockDefinitionStorage &definitions,
                         glm::ivec3 block_pos, BlockId block_id)
{
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

bool NeighborProvidesFluid(const UBlockWorld &blockWorld,
                           const UBlockDefinitionStorage &definitions,
                           glm::ivec3 neighbor_pos, BlockId fluid_id)
{
  const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
  if (neighbor_id == fluid_id)
  {
    return true;
  }
  if (IsFluidPermeableId(definitions, neighbor_id) &&
      CellHasActiveFluid(blockWorld, definitions, neighbor_pos))
  {
    return ResolveFluidKindImpl(blockWorld, definitions, neighbor_pos,
                                neighbor_id) == fluid_id;
  }
  return false;
}

BlockId ResolveFluidBlockIdImpl(const UBlockWorld &blockWorld,
                                const UBlockDefinitionStorage &definitions,
                                glm::ivec3 block_pos)
{
  const BlockId block_id = blockWorld.GetBlock(block_pos);
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid && def->Physics.FluidMaxLevel >= 7;
  }
  return false;
}

} // namespace

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

bool UFluidSpreadSystem::HasSpreadTarget(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  const BlockId block_id = blockWorld.GetBlock(block_pos);
  if (IsLiquidId(definitions, block_id))
  {
    const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
    if (below.y >= 0 &&
        UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions, below))
    {
      return true;
    }
    if (!HorizontalSpreadAllowed(blockWorld, definitions, block_pos))
    {
      return true;
    }
    static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
        glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
        glm::ivec3(0, 0, 1)};
    const FluidCellState self_state =
        EffectiveFluidState(blockWorld, block_pos);
    const int self_level = LevelFromState(self_state);
    const int max_level = GetFluidMaxLevel(definitions, block_id);
    if (self_state.IsSource() || self_level < max_level)
    {
      for (const glm::ivec3 &offset : kSideOffsets)
      {
        const glm::ivec3 side = block_pos + offset;
        const glm::ivec3 side_below(side.x, side.y - 1, side.z);
        if (side_below.y >= 0 &&
            UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions,
                                              side_below))
        {
          continue;
        }
        if (UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions, side))
        {
          return true;
        }
        if (NeighborProvidesFluid(blockWorld, definitions, side, block_id))
        {
          const FluidCellState neighbor_state =
              EffectiveFluidState(blockWorld, side);
          if (LevelFromState(neighbor_state) + 1 < self_level)
          {
            return true;
          }
        }
      }
    }
    return self_state.IsSource();
  }

  if (!UFluidFillPolicy::CanReceiveFluid(blockWorld, definitions, block_pos))
  {
    return false;
  }
  const BlockId fluid_id =
      ResolveFluidKind(blockWorld, definitions, block_pos, block_id);
  if (fluid_id == BLOCK_AIR)
  {
    return false;
  }
  if (IsFluidPermeableId(definitions, block_id) &&
      CellHasActiveFluid(blockWorld, definitions, block_pos))
  {
    const FluidCellState self_fluid = blockWorld.GetFluidState(block_pos);
    if (self_fluid.HasExplicitKind())
    {
      return HasOutboundSpreadTarget(blockWorld, definitions, block_pos,
                                     fluid_id);
    }
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    if (!NeighborProvidesFluid(blockWorld, definitions, neighbor_pos, fluid_id))
    {
      continue;
    }
    const FluidCellState neighbor_state =
        EffectiveFluidState(blockWorld, neighbor_pos);
    if (neighbor_state.IsSource())
    {
      return true;
    }
    if (LevelFromState(neighbor_state) + 1 <=
        GetFluidMaxLevel(definitions, fluid_id))
    {
      return true;
    }
  }
  return false;
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
  return ResolveWaterBlockIdImpl(definitions);
}

BlockId UFluidSpreadSystem::ResolveFluidKind(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos, BlockId block_id)
{
  return ResolveFluidKindImpl(blockWorld, definitions, block_pos, block_id);
}

FluidKind UFluidSpreadSystem::FluidKindFromBlockId(
    const UBlockDefinitionStorage &definitions, BlockId id)
{
  return FluidKindFromBlockIdImpl(definitions, id);
}

BlockId UFluidSpreadSystem::BlockIdFromFluidKind(
    const UBlockDefinitionStorage &definitions, FluidKind kind)
{
  return BlockIdFromFluidKindImpl(definitions, kind);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockId(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  return ResolveFluidBlockIdImpl(blockWorld, definitions, block_pos);
}

BlockId UFluidSpreadSystem::ResolveFluidBlockIdForMesh(
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

void UFluidSpreadSystem::ApplyFluidFill(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, BlockId fluid_id, FluidCellState state)
{
  UFluidFillPolicy::ApplyFluidFill(blockWorld, definitions, pos, fluid_id,
                                   state);
}

} // namespace cutum
