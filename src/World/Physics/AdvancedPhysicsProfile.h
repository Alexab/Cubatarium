#ifndef ADVANCEDPHYSICSPROFILE_H
#define ADVANCEDPHYSICSPROFILE_H

#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldBlockPhysicsService;

class UAdvancedPhysicsProfile
{
public:
  static void Configure(UWorldBlockPhysicsService &service,
                        PhysicsFeatureFlags flags);
};

} // namespace cutum

#endif // ADVANCEDPHYSICSPROFILE_H
