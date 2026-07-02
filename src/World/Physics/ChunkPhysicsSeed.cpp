#include "World/Physics/ChunkPhysicsSeed.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/PhysicsChunkDistance.h"

namespace cutum
{

void SeedPhysicsOnChunkCommitted(UWorld &world, glm::ivec3 chunk_coord,
                                 const ChunkPhysicsSeedBudgets &budgets)
{
  const glm::ivec3 focus = world.GetMovementDiagnostics().feetChunk;
  const int liquid_radius = world.GetPhysicsBudgets().LiquidUpdateRadiusChunks;
  const int falling_radius = world.GetPhysicsBudgets().FallingScanRadiusChunks;
  const int max_radius = std::max(liquid_radius, falling_radius);
  if (ChebyshevChunkDistance(chunk_coord, focus) > max_radius)
  {
    return;
  }

  const UBlockRegistry &registry = world.GetBlockRegistry();
  UBlockWorld &block_world = world.GetBlockWorld();
  const glm::ivec3 origin = chunk_coord * CHUNK_SIZE;
  int columns_scanned = 0;
  int liquid_enqueued = 0;

  for (int lz = 0; lz < CHUNK_SIZE && columns_scanned < budgets.MaxColumnsPerCommit;
       ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE && columns_scanned < budgets.MaxColumnsPerCommit;
         ++lx)
    {
      ++columns_scanned;
      int top_y = -1;
      BlockId top_id = BLOCK_AIR;
      for (int ly = CHUNK_SIZE - 1; ly >= 0; --ly)
      {
        const glm::ivec3 pos = origin + glm::ivec3(lx, ly, lz);
        const BlockId id = block_world.GetBlock(pos);
        if (id != BLOCK_AIR)
        {
          top_y = ly;
          top_id = id;
          break;
        }
      }
      if (top_y < 0)
      {
        continue;
      }
      const glm::ivec3 top_pos = origin + glm::ivec3(lx, top_y, lz);
      if (registry.IsLiquid(top_id) &&
          liquid_enqueued < budgets.MaxLiquidEnqueuePerCommit)
      {
        FluidCellState fluid_state = block_world.GetFluidState(top_pos);
        if (PackFluidCellState(fluid_state) == 0)
        {
          fluid_state = FluidCellState::Source();
        }
        if (fluid_state.IsSource() ||
            UFluidSpreadSystem::HasSpreadTarget(block_world, *registry.GetDefinitions(),
                                                top_pos))
        {
          world.TryEnqueueFluidAt(top_pos);
          ++liquid_enqueued;
        }
      }
      else if (registry.IsFallingBlock(top_id))
      {
        world.TrySeedFallingAt(top_pos);
      }
    }
  }
}

} // namespace cutum
