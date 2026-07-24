#include "World/Physics/AdvancedPhysicsProfile.h"
#include "World/Physics/WorldBlockPhysicsService.h"

namespace cutum
{

void UAdvancedPhysicsProfile::Configure(UWorldBlockPhysicsService &service,
                                        PhysicsFeatureFlags flags)
{
  flags.EnableBlockEvents = true;
  flags.EnableFalling = true;
  flags.EnableFluids = true;
  flags.EnableMaterialRules = true;
  service.SetFeatureFlags(flags);
}

} // namespace cutum
