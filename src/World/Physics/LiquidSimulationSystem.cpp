#include "World/Physics/LiquidSimulationSystem.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/World.h"
#include <array>

namespace cutum
{

namespace
{

bool CanAcceptLiquid(const UBlockWorld &blockWorld, const UBlockRegistry &registry,
                     glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  return registry.IsFloodable(id) && !registry.IsLiquid(id);
}

void MarkApplied(LiquidSimulationStats &stats, glm::ivec3 dest, bool sourceCleared)
{
  ++stats.Applied;
  stats.AppliedDest = dest;
  stats.HasAppliedDest = true;
  stats.SourceCleared = sourceCleared;
}

} // namespace

bool ULiquidSimulationSystem::HasFlowTarget(UWorld &world, glm::ivec3 blockPos)
{
  UBlockWorld &blockWorld = world.GetBlockWorld();
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const BlockId id = blockWorld.GetBlock(blockPos);
  if (!registry.IsLiquid(id))
  {
    return false;
  }

  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  if (below.y >= 0 && CanAcceptLiquid(blockWorld, registry, below))
  {
    return true;
  }

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};
  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = blockPos + offset;
    if (!CanAcceptLiquid(blockWorld, registry, side))
    {
      continue;
    }
    const glm::ivec3 sideBelow(side.x, side.y - 1, side.z);
    if (sideBelow.y >= 0 && CanAcceptLiquid(blockWorld, registry, sideBelow))
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
  LiquidSimulationStats stats;
  ++stats.Candidates;
  if (ShadowMode)
  {
    ++stats.Deferred;
    return stats;
  }

  UBlockWorld &blockWorld = world.GetBlockWorld();
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const BlockId id = blockWorld.GetBlock(blockPos);
  if (!registry.IsLiquid(id))
  {
    return stats;
  }

  const bool renewable = registry.IsLiquidRenewable(id);

  const glm::ivec3 below(blockPos.x, blockPos.y - 1, blockPos.z);
  if (below.y >= 0 && CanAcceptLiquid(blockWorld, registry, below))
  {
    if (!renewable)
    {
      blockWorld.SetBlock(blockPos, BLOCK_AIR);
    }
    blockWorld.SetBlock(below, id);
    MarkApplied(stats, below, !renewable);
    return stats;
  }

  static constexpr std::array<glm::ivec3, 4> kSideOffsets = {
      glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, -1),
      glm::ivec3(0, 0, 1)};
  for (const glm::ivec3 &offset : kSideOffsets)
  {
    const glm::ivec3 side = blockPos + offset;
    if (!CanAcceptLiquid(blockWorld, registry, side))
    {
      continue;
    }
    const glm::ivec3 sideBelow(side.x, side.y - 1, side.z);
    if (sideBelow.y >= 0 && CanAcceptLiquid(blockWorld, registry, sideBelow))
    {
      continue;
    }
    if (!renewable)
    {
      blockWorld.SetBlock(blockPos, BLOCK_AIR);
    }
    blockWorld.SetBlock(side, id);
    MarkApplied(stats, side, !renewable);
    return stats;
  }

  return stats;
}

} // namespace cutum
