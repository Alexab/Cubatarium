#include "World/Physics/LiquidSimulationSystem.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Physics/LiquidDebugTrace.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

bool CanAcceptLiquidFromDefinitions(const UBlockWorld &blockWorld,
                                    const UBlockDefinitionStorage &definitions,
                                    glm::ivec3 pos)
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

bool CanAcceptLiquid(const UBlockWorld &blockWorld, const UBlockRegistry &registry,
                     glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return false;
  }
  return CanAcceptLiquidFromDefinitions(blockWorld, *definitions, pos);
}

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
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

bool IsFloodable(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable;
  }
  return false;
}

float GetLiquidViscosity(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return std::max(1.0f, def->Physics.LiquidViscosity);
  }
  return 1.0f;
}

bool CanAcceptLiquid(const UBlockWorld &blockWorld,
                     const UBlockDefinitionStorage &definitions, glm::ivec3 pos)
{
  return CanAcceptLiquidFromDefinitions(blockWorld, definitions, pos);
}

void MarkApplied(LiquidSimulationStats &stats, glm::ivec3 dest, bool sourceCleared)
{
  ++stats.Applied;
  stats.AppliedDest = dest;
  stats.HasAppliedDest = true;
  stats.SourceCleared = sourceCleared;
}

uint32_t HashBlockPos(glm::ivec3 block_pos)
{
  const uint32_t x = static_cast<uint32_t>(block_pos.x);
  const uint32_t y = static_cast<uint32_t>(block_pos.y);
  const uint32_t z = static_cast<uint32_t>(block_pos.z);
  return x * 73856093u ^ y * 19349663u ^ z * 83492791u;
}

} // namespace

bool ULiquidSimulationSystem::HasFlowTarget(UWorld &world, glm::ivec3 blockPos)
{
  UBlockWorld &blockWorld = world.GetBlockWorld();
  const UBlockDefinitionStorage *definitions =
      world.GetBlockRegistry().GetDefinitions();
  if (definitions == nullptr)
  {
    return false;
  }
  const BlockId id = blockWorld.GetBlock(blockPos);
  if (!IsLiquidId(*definitions, id))
  {
    return false;
  }

  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  if (below.y >= 0 && CanAcceptLiquid(blockWorld, *definitions, below))
  {
    return true;
  }

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};
  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = blockPos + offset;
    if (!CanAcceptLiquid(blockWorld, *definitions, side))
    {
      continue;
    }
    const glm::ivec3 sideBelow(side.x, side.y - 1, side.z);
    if (sideBelow.y >= 0 && CanAcceptLiquid(blockWorld, *definitions, sideBelow))
    {
      continue;
    }
    return true;
  }

  return false;
}

LiquidSimulationStats ULiquidSimulationSystem::Tick(UWorld &world,
                                                    glm::ivec3 blockPos)
{
  const UBlockDefinitionStorage *definitions =
      world.GetBlockRegistry().GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(world.GetBlockWorld(), *definitions,
                   world.GetPhysicsTickCounter(), blockPos);
}

LiquidSimulationStats ULiquidSimulationSystem::TickBlock(
    UBlockWorld &blockWorld, const UBlockRegistry &registry,
    uint64_t physics_tick, glm::ivec3 blockPos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return {};
  }
  return TickBlock(blockWorld, *definitions, physics_tick, blockPos);
}

LiquidSimulationStats ULiquidSimulationSystem::TickBlock(
    UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    uint64_t physics_tick, glm::ivec3 blockPos)
{
  LiquidSimulationStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    ++stats.Deferred;
    return stats;
  }

  const BlockId id = blockWorld.GetBlock(blockPos);
  if (!IsLiquidId(definitions, id))
  {
    return stats;
  }

  const float viscosity = GetLiquidViscosity(definitions, id);
  if (!ShouldProcessLiquidTick(physics_tick, blockPos, viscosity))
  {
    return stats;
  }

  const bool renewable = IsLiquidRenewable(definitions, id);

  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  if (below.y >= 0 && CanAcceptLiquid(blockWorld, definitions, below))
  {
    if (!renewable)
    {
      blockWorld.SetBlock(blockPos, BLOCK_AIR);
    }
    blockWorld.SetBlock(below, id);
    MarkApplied(stats, below, !renewable);
    if (DebugTraceEnabled)
    {
      ULiquidDebugTrace::Instance().Record(blockPos, below, "down");
    }
    return stats;
  }

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};
  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = blockPos + offset;
    if (!CanAcceptLiquid(blockWorld, definitions, side))
    {
      continue;
    }
    const glm::ivec3 sideBelow(side.x, side.y - 1, side.z);
    if (sideBelow.y >= 0 && CanAcceptLiquid(blockWorld, definitions, sideBelow))
    {
      continue;
    }
    if (!renewable)
    {
      blockWorld.SetBlock(blockPos, BLOCK_AIR);
    }
    blockWorld.SetBlock(side, id);
    MarkApplied(stats, side, !renewable);
    if (DebugTraceEnabled)
    {
      ULiquidDebugTrace::Instance().Record(blockPos, side, "side");
    }
    return stats;
  }

  return stats;
}

} // namespace cutum
