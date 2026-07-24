#include "World/Physics/PhysicsProfile.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "physics_profile_parse_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::PhysicsProfile;
  Expect(cutum::PhysicsProfileFromString("primitive") ==
             PhysicsProfile::Primitive,
         "primitive parse failed");
  Expect(cutum::PhysicsProfileFromString("standard") ==
             PhysicsProfile::Standard,
         "standard parse failed");
  Expect(cutum::PhysicsProfileFromString("advanced") ==
             PhysicsProfile::Advanced,
         "advanced parse failed");
  Expect(cutum::PhysicsProfileFromString("unknown") ==
             PhysicsProfile::Primitive,
         "fallback parse failed");
  std::cout << "physics_profile_parse_test: OK" << std::endl;
  return 0;
}
