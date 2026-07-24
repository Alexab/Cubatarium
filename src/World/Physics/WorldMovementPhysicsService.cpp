#include "World/Physics/WorldMovementPhysicsService.h"
#include "World/Core/World.h"

namespace cutum
{

void UWorldMovementPhysicsService::TickMovement(UWorld &world)
{
  world.RunLegacyPhysicsFrame();
}

} // namespace cutum
