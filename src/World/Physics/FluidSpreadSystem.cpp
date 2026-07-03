#include "World/Physics/FluidSpreadSystem.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <algorithm>
#include <array>

namespace cutum
{

namespace
{

constexpr int kWaterDropBoost = 4;

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

bool IsFluidPermeableId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (id == BLOCK_AIR || IsLiquidId(definitions, id))
  {
    return false;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    if (def->Physics.Movement.Occupancy >= 1.0f)
    {
      return false;
    }
    return def->Render.Style == BlockRenderStyle::Cross ||
           def->Render.Style == BlockRenderStyle::Cutout;
  }
  return false;
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
    return PackFluidCellState(blockWorld.GetFluidState(pos)) != 0;
  }
  return false;
}

bool ShouldReplaceBlockWithFluidImpl(const UBlockWorld &blockWorld,
                                     const UBlockDefinitionStorage &definitions,
                                     glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return false;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
}

FluidCellState StoredFluidStateForCell(const UBlockWorld &blockWorld,
                                       const UBlockDefinitionStorage &definitions,
                                       glm::ivec3 pos, FluidCellState state)
{
  if (ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos))
  {
    return state;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id) && state.IsSource())
  {
    return FluidCellState::Flowing(1);
  }
  return state;
}

bool CanReceiveFluidImpl(const UBlockWorld &blockWorld,
                         const UBlockDefinitionStorage &definitions,
                         glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return true;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
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
  return static_cast<int>(FLUID_LEVEL_MAX) + 1 -
         GetLiquidRange(definitions, id);
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
                                   nb_level + kWaterDropBoost);
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
  return below.y < 0 || !CanReceiveFluidImpl(blockWorld, definitions, below);
}

BlockId ResolveFluidKind(const UBlockWorld &blockWorld,
                         const UBlockDefinitionStorage &definitions,
                         glm::ivec3 block_pos, BlockId block_id)
{
  if (IsLiquidId(definitions, block_id))
  {
    return block_id;
  }
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  if (CellHasActiveFluid(blockWorld, definitions, block_pos))
  {
    for (const glm::ivec3 &offset : kDirs)
    {
      const BlockId neighbor_id = blockWorld.GetBlock(block_pos + offset);
      if (IsLiquidId(definitions, neighbor_id))
      {
        return neighbor_id;
      }
    }
  }
  for (const glm::ivec3 &offset : kDirs)
  {
    const glm::ivec3 neighbor_pos = block_pos + offset;
    const BlockId neighbor_id = blockWorld.GetBlock(neighbor_pos);
    if (IsLiquidId(definitions, neighbor_id))
    {
      return neighbor_id;
    }
    if (IsFluidPermeableId(definitions, neighbor_id) &&
        CellHasActiveFluid(blockWorld, definitions, neighbor_pos))
    {
      const BlockId kind =
          ResolveFluidKind(blockWorld, definitions, neighbor_pos, neighbor_id);
      if (kind != BLOCK_AIR)
      {
        return kind;
      }
    }
  }
  return BLOCK_AIR;
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
    return ResolveFluidKind(blockWorld, definitions, neighbor_pos,
                            neighbor_id) == fluid_id;
  }
  return false;
}

void ApplyFluidFill(UBlockWorld &blockWorld,
                    const UBlockDefinitionStorage &definitions,
                    glm::ivec3 pos, BlockId fluid_id, FluidCellState state)
{
  const FluidCellState stored =
      StoredFluidStateForCell(blockWorld, definitions, pos, state);
  if (ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos))
  {
    blockWorld.SetBlock(pos, fluid_id);
  }
  blockWorld.SetFluidState(pos, stored);
}

} // namespace

bool UFluidSpreadSystem::CanReceiveFluid(const UBlockWorld &blockWorld,
                                         const UBlockRegistry &registry,
                                         glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return blockWorld.IsAir(pos);
  }
  return CanReceiveFluidImpl(blockWorld, *definitions, pos);
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
  return ShouldReplaceBlockWithFluidImpl(blockWorld, *definitions, pos);
}

bool UFluidSpreadSystem::CanReceiveFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return CanReceiveFluidImpl(blockWorld, definitions, pos);
}

bool UFluidSpreadSystem::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  return ShouldReplaceBlockWithFluidImpl(blockWorld, definitions, pos);
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
    if (below.y >= 0 && CanReceiveFluidImpl(blockWorld, definitions, below))
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
            CanReceiveFluidImpl(blockWorld, definitions, side_below))
        {
          continue;
        }
        if (CanReceiveFluidImpl(blockWorld, definitions, side))
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

  if (!CanReceiveFluidImpl(blockWorld, definitions, block_pos))
  {
    return false;
  }
  const BlockId fluid_id =
      ResolveFluidKind(blockWorld, definitions, block_pos, block_id);
  if (fluid_id == BLOCK_AIR)
  {
    return false;
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
                   world.GetPhysicsTickCounter(), block_pos);
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
  return TickBlock(blockWorld, *definitions, physics_tick, block_pos);
}

FluidSpreadStats
UFluidSpreadSystem::TickBlock(UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              uint64_t physics_tick, glm::ivec3 block_pos)
{
  FluidSpreadStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    return stats;
  }

  const BlockId block_id = blockWorld.GetBlock(block_pos);
  const bool is_liquid = IsLiquidId(definitions, block_id);
  const bool is_waterlogged_permeable =
      !is_liquid && IsFluidPermeableId(definitions, block_id) &&
      CellHasActiveFluid(blockWorld, definitions, block_pos);
  const bool is_floodable =
      CanReceiveFluidImpl(blockWorld, definitions, block_pos);
  if (!is_liquid && !is_floodable)
  {
    return stats;
  }

  const BlockId fluid_id =
      ResolveFluidKind(blockWorld, definitions, block_pos, block_id);
  if (fluid_id == BLOCK_AIR)
  {
    return stats;
  }

  const int spread_period = GetSpreadPeriod(definitions, fluid_id);
  if (is_liquid &&
      !ShouldProcessFluidTick(physics_tick, block_pos, spread_period))
  {
    return stats;
  }

  const int max_level = GetFluidMaxLevel(definitions, fluid_id);
  const int min_survive = MinSurviveLevel(definitions, fluid_id);
  const int viscosity = GetLiquidViscosity(definitions, fluid_id);
  const bool renewable = IsLiquidRenewable(definitions, fluid_id);

  FluidCellState current_state =
      (is_liquid || is_waterlogged_permeable)
          ? EffectiveFluidState(blockWorld, block_pos)
          : FluidCellState::Flowing(static_cast<uint8_t>(max_level + 1));
  const int current_level =
      (is_liquid || is_waterlogged_permeable)
          ? LevelFromState(current_state)
          : max_level + 1;

  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  const bool below_floodable =
      below.y >= 0 && CanReceiveFluidImpl(blockWorld, definitions, below);
  bool flowing_down = below_floodable;

  if (is_liquid && below_floodable)
  {
    const FluidCellState below_state = FluidCellState::Flowing(1, true);
    ApplyFluidFill(blockWorld, definitions, below, fluid_id, below_state);
    RecordChange(stats, block_pos, below, fluid_id, below_state, false,
                 "spread_down");
    return stats;
  }

  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  static constexpr std::array<NeighborAxis, 6> kAxes = {
      NeighborAxis::Upper,      NeighborAxis::Horizontal,
      NeighborAxis::Horizontal, NeighborAxis::Horizontal,
      NeighborAxis::Horizontal, NeighborAxis::Lower};

  int source_count = 0;
  bool source_not_below = false;
  int max_incoming_level = 0;

  for (size_t i = 0; i < kDirs.size(); ++i)
  {
    const glm::ivec3 neighbor_pos = block_pos + kDirs[i];
    if (!NeighborProvidesFluid(blockWorld, definitions, neighbor_pos, fluid_id))
    {
      continue;
    }
    const FluidCellState neighbor_state =
        EffectiveFluidState(blockWorld, neighbor_pos);
    if (neighbor_state.IsSource())
    {
      if (kAxes[i] != NeighborAxis::Lower)
      {
        ++source_count;
        source_not_below = true;
      }
      continue;
    }
    if (is_liquid && kAxes[i] == NeighborAxis::Horizontal &&
        !HorizontalSpreadAllowed(blockWorld, definitions, block_pos))
    {
      continue;
    }
    const glm::ivec3 side_below(neighbor_pos.x, neighbor_pos.y - 1,
                                neighbor_pos.z);
    if (is_liquid && kAxes[i] == NeighborAxis::Horizontal &&
        side_below.y >= 0 &&
        CanReceiveFluidImpl(blockWorld, definitions, side_below))
    {
      continue;
    }
    max_incoming_level =
        MaxLevelFromNeighbor(neighbor_state, kAxes[i], max_incoming_level);
  }

  bool new_is_source = false;
  int new_level = 0;

  if ((source_count >= 2 && renewable) ||
      (is_liquid && current_state.IsSource()))
  {
    new_is_source = true;
  }
  else if (source_count >= 1 && source_not_below)
  {
    new_level = 1;
  }
  else
  {
    new_level = max_incoming_level;
    if (is_liquid && viscosity > 1 && new_level != current_level &&
        new_level > 0)
    {
      const int level_inc = new_level - current_level;
      if (level_inc < -viscosity || level_inc > viscosity)
      {
        new_level = current_level + level_inc / viscosity;
      }
      else if (level_inc < 0)
      {
        new_level = current_level - 1;
      }
      else if (level_inc > 0)
      {
        new_level = current_level + 1;
      }
    }
  }

  if (new_is_source)
  {
    if (!is_liquid || !current_state.IsSource())
    {
      ApplyFluidFill(blockWorld, definitions, block_pos, fluid_id,
                   FluidCellState::Source());
      RecordChange(stats, block_pos, block_pos, fluid_id,
                   FluidCellState::Source(), false, "transform_source");
    }
    return stats;
  }

  if (new_level <= 0 || new_level < min_survive || new_level > max_level)
  {
    if (is_liquid)
    {
      blockWorld.SetBlock(block_pos, BLOCK_AIR);
      blockWorld.ClearFluidState(block_pos);
      RecordChange(stats, block_pos, block_pos, fluid_id, FluidCellState{},
                   true, "transform_dry");
    }
    else if (is_waterlogged_permeable)
    {
      blockWorld.ClearFluidState(block_pos);
      RecordChange(stats, block_pos, block_pos, fluid_id, FluidCellState{},
                   true, "transform_dry");
    }
    return stats;
  }

  const FluidCellState new_state =
      FluidCellState::Flowing(static_cast<uint8_t>(new_level), flowing_down);
  const bool unchanged = (is_liquid || is_waterlogged_permeable) &&
                         !current_state.IsSource() &&
                         current_state.Level == new_state.Level &&
                         current_state.Falling == new_state.Falling;
  if (unchanged)
  {
    return stats;
  }

  ApplyFluidFill(blockWorld, definitions, block_pos, fluid_id, new_state);
  RecordChange(stats, block_pos, block_pos, fluid_id, new_state, false,
               is_liquid ? "transform_flow" : "transform_flood");
  return stats;
}

} // namespace cutum
