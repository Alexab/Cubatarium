#include "World/Physics/WorldPhysicsScheduler.h"
#include "World/Physics/IUBlockPhysicsService.h"
#include "World/Physics/IUMovementPhysicsService.h"

namespace cutum
{

UWorldPhysicsScheduler::UWorldPhysicsScheduler(
    IUMovementPhysicsService *movementService, IUBlockPhysicsService *blockService)
    : MovementService(movementService), BlockService(blockService)
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
}

} // namespace cutum
