#include "World/Physics/PrimitivePhysicsProfile.h"
#include "World/Physics/WorldBlockPhysicsService.h"

namespace cutum
{

void UPrimitivePhysicsProfile::Configure(UWorldBlockPhysicsService &service,
                                         PhysicsFeatureFlags flags)
{
  flags.EnableFalling = false;
  flags.EnableFluids = false;
  flags.EnableBlockEvents = false;
  service.SetFeatureFlags(flags);
}

} // namespace cutum
