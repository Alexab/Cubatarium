#include "World/Physics/PhysicsProfileFactory.h"
#include "World/Physics/AdvancedPhysicsProfile.h"
#include "World/Physics/PrimitivePhysicsProfile.h"
#include "World/Physics/StandardPhysicsProfile.h"
#include "World/Physics/WorldBlockPhysicsService.h"

namespace cutum
{

void UPhysicsProfileFactory::ConfigureService(PhysicsProfile profile,
                                              UWorldBlockPhysicsService &service,
                                              PhysicsFeatureFlags flags)
{
  switch (profile)
  {
  case PhysicsProfile::Primitive:
    UPrimitivePhysicsProfile::Configure(service, flags);
    break;
  case PhysicsProfile::Standard:
    UStandardPhysicsProfile::Configure(service, flags);
    break;
  case PhysicsProfile::Advanced:
    UAdvancedPhysicsProfile::Configure(service, flags);
    break;
  }
}

} // namespace cutum
