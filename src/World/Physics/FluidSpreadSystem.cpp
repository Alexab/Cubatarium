#include "World/Physics/FluidSpreadSystem.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/LiquidDebugTrace.h"

#include <array>
#include <algorithm>

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

bool CanAcceptFluid(const UBlockWorld &blockWorld,
                    const UBlockDefinitionStorage &definitions, glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
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

void RecordChange(FluidSpreadStats &stats, glm::ivec3 block_pos,
                  glm::ivec3 neighbor_pos, BlockId fluid_id,
                  FluidCellState new_state, bool removed_fluid,
                  const char *reason)
{
  ++stats.Applied;
  stats.Changes.push_back({block_pos, neighbor_pos, fluid_id, new_state,
                           removed_fluid});
  ULiquidDebugTrace::Instance().Record(block_pos, neighbor_pos, reason);
}

} // namespace

bool UFluidSpreadSystem::ShouldProcessFluidTick(uint64_t physics_tick,
                                                glm::ivec3 block_pos,
                                                int spread_period)
{
  const int period = std::max(1, spread_period);
  const uint32_t x = static_cast<uint32_t>(block_pos.x);
  const uint32_t y = static_cast<uint32_t>(block_pos.y);
  const uint32_t z = static_cast<uint32_t>(block_pos.z);
  const uint32_t phase =
      (x * 73856093u ^ y * 19349663u ^ z * 83492791u) %
      static_cast<uint32_t>(period);
  return (physics_tick + phase) % static_cast<uint32_t>(period) == 0;
}

bool UFluidSpreadSystem::HasSpreadTarget(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 block_pos)
{
  const BlockId id = blockWorld.GetBlock(block_pos);
  if (!IsLiquidId(definitions, id))
  {
    return false;
  }

  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  if (below.y >= 0 && CanAcceptFluid(blockWorld, definitions, below))
  {
    return true;
  }

  const FluidCellState state = EffectiveFluidState(blockWorld, block_pos);
  if (state.Falling != 0)
  {
    return false;
  }

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};
  const int max_level = GetFluidMaxLevel(definitions, id);
  const uint8_t new_level =
      static_cast<uint8_t>(std::min<int>(state.Level + 1, max_level));
  if (new_level > max_level || (state.Level != 0 && new_level > FLUID_LEVEL_MAX))
  {
    return false;
  }

  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = block_pos + offset;
    if (!blockWorld.IsAir(side))
    {
      if (IsLiquidId(definitions, blockWorld.GetBlock(side)))
      {
        const FluidCellState neighbor_state =
            EffectiveFluidState(blockWorld, side);
        if (neighbor_state.Level > new_level)
        {
          continue;
        }
      }
      continue;
    }
    return true;
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

FluidSpreadStats UFluidSpreadSystem::TickBlock(
    UBlockWorld &blockWorld, const UBlockRegistry &registry,
    uint64_t physics_tick, glm::ivec3 block_pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(blockWorld, *definitions, physics_tick, block_pos);
}

FluidSpreadStats UFluidSpreadSystem::TickBlock(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    uint64_t physics_tick, glm::ivec3 block_pos)
{
  FluidSpreadStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    return stats;
  }

  const BlockId id = blockWorld.GetBlock(block_pos);
  if (!IsLiquidId(definitions, id))
  {
    return stats;
  }

  const int spread_period = GetSpreadPeriod(definitions, id);
  if (!ShouldProcessFluidTick(physics_tick, block_pos, spread_period))
  {
    return stats;
  }

  const int max_level = GetFluidMaxLevel(definitions, id);
  FluidCellState state = EffectiveFluidState(blockWorld, block_pos);

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};

  const glm::ivec3 below(block_pos.x, block_pos.y - 1, block_pos.z);
  if (below.y >= 0 && CanAcceptFluid(blockWorld, definitions, below))
  {
    const FluidCellState below_state = FluidCellState::Flowing(1, true);
    blockWorld.SetBlock(below, id);
    blockWorld.SetFluidState(below, below_state);
    RecordChange(stats, block_pos, below, id, below_state, false, "spread_down");
    return stats;
  }

  if (state.Falling != 0)
  {
    return stats;
  }

  const uint8_t new_level =
      static_cast<uint8_t>(std::min<int>(state.Level + 1, max_level));
  if (new_level == 0 || new_level > FLUID_LEVEL_MAX)
  {
    return stats;
  }

  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = block_pos + offset;
    if (!blockWorld.IsAir(side))
    {
      if (IsLiquidId(definitions, blockWorld.GetBlock(side)))
      {
        const FluidCellState neighbor_state =
            EffectiveFluidState(blockWorld, side);
        if (neighbor_state.Level <= new_level)
        {
          continue;
        }
      }
      continue;
    }
    const FluidCellState side_state = FluidCellState::Flowing(new_level, false);
    blockWorld.SetBlock(side, id);
    blockWorld.SetFluidState(side, side_state);
    RecordChange(stats, block_pos, side, id, side_state, false, "spread_side");
    return stats;
  }

  return stats;
}

} // namespace cutum
