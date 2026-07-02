#ifndef STANDARDPHYSICSPROFILE_H
#define STANDARDPHYSICSPROFILE_H

#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldBlockPhysicsService;

class UStandardPhysicsProfile
{
public:
  static void Configure(UWorldBlockPhysicsService &service,
                        PhysicsFeatureFlags flags);
};

} // namespace cutum

#endif // STANDARDPHYSICSPROFILE_H
