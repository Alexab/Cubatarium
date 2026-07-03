#include <array>

#include "World/Physics/FluidReflowScan.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"

namespace cutum
{

namespace
{

bool ShouldEnqueueFluidCell(const UBlockWorld &block_world,
                            const UBlockRegistry &registry, glm::ivec3 pos)
{
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return false;
  }
  const BlockId id = block_world.GetBlock(pos);
  if (registry.IsLiquid(id))
  {
    return true;
  }
  if (UFluidSpreadSystem::CanReceiveFluid(block_world, *definitions, pos) &&
      UFluidSpreadSystem::HasSpreadTarget(block_world, *definitions, pos))
  {
    return true;
  }
  return false;
}

void TryEnqueueCell(UWorld &world, glm::ivec3 pos)
{
  const UBlockRegistry &registry = world.GetBlockRegistry();
  if (ShouldEnqueueFluidCell(world.GetBlockWorld(), registry, pos))
  {
    world.ForceEnqueueFluidAt(pos);
  }
}

void ScanColumn(UWorld &world, glm::ivec3 column_base, int max_enqueue,
                int &enqueued)
{
  if (enqueued >= max_enqueue)
  {
    return;
  }
  const UBlockWorld &block_world = world.GetBlockWorld();
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return;
  }

  for (int y = CHUNK_SIZE - 1; y >= 0; --y)
  {
    const glm::ivec3 pos = column_base + glm::ivec3(0, y, 0);
    const BlockId id = block_world.GetBlock(pos);
    if (id == BLOCK_AIR)
    {
      continue;
    }
    if (registry.IsLiquid(id))
    {
      TryEnqueueCell(world, pos);
      ++enqueued;
      break;
    }
    if (UFluidSpreadSystem::CanReceiveFluid(block_world, *definitions, pos))
    {
      if (UFluidSpreadSystem::HasSpreadTarget(block_world, *definitions, pos))
      {
        TryEnqueueCell(world, pos);
        ++enqueued;
      }
      break;
    }
    break;
  }
}

} // namespace

void EnqueueFluidFrontierAt(UWorld &world, glm::ivec3 block_pos)
{
  TryEnqueueCell(world, block_pos);
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    TryEnqueueCell(world, block_pos + offset);
  }
}

void ScanChunkFluidFrontier(UWorld &world, glm::ivec3 chunk_coord,
                            int max_enqueue)
{
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  int enqueued = 0;
  for (int lz = 0; lz < CHUNK_SIZE && enqueued < max_enqueue; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE && enqueued < max_enqueue; ++lx)
    {
      ScanColumn(world, origin + glm::ivec3(lx, 0, lz), max_enqueue, enqueued);
    }
  }
  for (int i = 0; i < CHUNK_SIZE && enqueued < max_enqueue; ++i)
  {
    ScanColumn(world, origin + glm::ivec3(i, 0, -1), max_enqueue, enqueued);
    ScanColumn(world, origin + glm::ivec3(i, 0, CHUNK_SIZE), max_enqueue,
               enqueued);
    ScanColumn(world, origin + glm::ivec3(-1, 0, i), max_enqueue, enqueued);
    ScanColumn(world, origin + glm::ivec3(CHUNK_SIZE, 0, i), max_enqueue,
               enqueued);
  }
}

} // namespace cutum
