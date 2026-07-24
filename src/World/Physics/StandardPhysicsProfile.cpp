#include "World/Physics/StandardPhysicsProfile.h"
#include "World/Physics/WorldBlockPhysicsService.h"

namespace cutum
{

void UStandardPhysicsProfile::Configure(UWorldBlockPhysicsService &service,
                                        PhysicsFeatureFlags flags)
{
  flags.EnableBlockEvents = true;
  service.SetFeatureFlags(flags);
}

} // namespace cutum
