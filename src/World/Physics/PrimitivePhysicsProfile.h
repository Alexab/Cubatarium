#ifndef PRIMITIVEPHYSICSPROFILE_H
#define PRIMITIVEPHYSICSPROFILE_H

#include "World/Physics/PhysicsProfile.h"

namespace cutum
{

class UWorldBlockPhysicsService;

class UPrimitivePhysicsProfile
{
public:
  static void Configure(UWorldBlockPhysicsService &service,
                        PhysicsFeatureFlags flags);
};

} // namespace cutum

#endif // PRIMITIVEPHYSICSPROFILE_H
