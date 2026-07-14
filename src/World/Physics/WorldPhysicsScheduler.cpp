#include "World/Physics/WorldPhysicsScheduler.h"
#include "World/Physics/IUBlockPhysicsService.h"
#include "World/Physics/IUChunkDirtyService.h"
#include "World/Physics/IUMovementPhysicsService.h"

namespace cutum
{

UWorldPhysicsScheduler::UWorldPhysicsScheduler(
    IUMovementPhysicsService *movementService, IUBlockPhysicsService *blockService,
    IUChunkDirtyService *chunkDirtyService)
    : MovementService(movementService), BlockService(blockService),
      ChunkDirtyService(chunkDirtyService)
{
}

void UWorldPhysicsScheduler::Tick(UWorld &world)
{
  if (MovementService)
  {
    MovementService->TickMovement(world);
  }
  if (BlockService)
  {
    BlockService->TickBlockPhysics(world);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->DrainRebuildQueues(world);
  }
}

} // namespace cutum
