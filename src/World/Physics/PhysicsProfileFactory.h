#ifndef PHYSICSPROFILEFACTORY_H
#define PHYSICSPROFILEFACTORY_H

#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldBlockPhysicsService;

class UPhysicsProfileFactory
{
public:
  static void ConfigureService(PhysicsProfile profile,
                               UWorldBlockPhysicsService &service,
                               PhysicsFeatureFlags flags);
};

} // namespace cutum

#endif // PHYSICSPROFILEFACTORY_H
