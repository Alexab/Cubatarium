#include "World/Physics/ChunkPhysicsSeed.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/PhysicsChunkDistance.h"

namespace cutum
{

void SeedPhysicsOnChunkCommitted(UWorld &world, glm::ivec3 chunk_coord,
                                 const ChunkPhysicsSeedBudgets &budgets)
{
  ScanChunkFluidFrontier(world, chunk_coord, budgets.MaxLiquidEnqueuePerCommit);

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

  for (int lz = 0;
       lz < CHUNK_SIZE && columns_scanned < budgets.MaxColumnsPerCommit; ++lz)
  {
    for (int lx = 0;
         lx < CHUNK_SIZE && columns_scanned < budgets.MaxColumnsPerCommit; ++lx)
    {
      ++columns_scanned;
      for (int ly = CHUNK_SIZE - 1; ly >= 0; --ly)
      {
        const glm::ivec3 pos = origin + glm::ivec3(lx, ly, lz);
        const BlockId id = block_world.GetBlock(pos);
        if (id == BLOCK_AIR)
        {
          continue;
        }
        if (registry.IsFallingBlock(id))
        {
          world.TrySeedFallingAt(pos);
        }
        break;
      }
    }
  }
}

} // namespace cutum
