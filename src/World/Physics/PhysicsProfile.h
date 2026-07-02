#ifndef PHYSICSPROFILE_H
#define PHYSICSPROFILE_H

#include <string>

namespace cutum
{

enum class PhysicsProfile
{
  Primitive,
  Standard,
  Advanced
};

inline std::string ToString(PhysicsProfile profile)
{
  switch (profile)
  {
  case PhysicsProfile::Primitive:
    return "primitive";
  case PhysicsProfile::Standard:
    return "standard";
  case PhysicsProfile::Advanced:
    return "advanced";
  }
  return "primitive";
}

inline PhysicsProfile PhysicsProfileFromString(const std::string &value)
{
  if (value == "standard")
  {
    return PhysicsProfile::Standard;
  }
  if (value == "advanced")
  {
    return PhysicsProfile::Advanced;
  }
  return PhysicsProfile::Primitive;
}

struct PhysicsFeatureFlags
{
  bool EnableFalling{false};
  bool EnableFluids{false};
  bool EnableBlockEvents{false};
  bool EnableCollisionBroadphase{false};
  bool EnableCollisionReadinessGate{false};
  bool FallingShadowMode{true};
  bool LiquidShadowMode{true};
  bool LiquidDebugTrace{false};
  bool EnableCollisionDda{false};
  bool EnableMaterialRules{false};
};

struct PhysicsBudgets
{
  int BlockEventsPerTickMax{128};
  int BlockQueueSoftLimit{2048};
  int BlockQueueHardLimit{8192};
  int LiquidEventsPerTickMax{128};
  int FluidBlocksPerTickMax{512};
  int LiquidQueueSoftLimit{2048};
  int LiquidQueueHardLimit{8192};
  int FallingEventsPerTickMax{128};
  int CollisionSafetyRadiusChunks{1};
  int VisualRemeshPerTickMax{8};
  int CollisionRebuildPerTickMax{16};
  int VisualRemeshQueueSoftLimit{512};
  int VisualRemeshQueueHardLimit{4096};
  int CollisionRebuildQueueSoftLimit{512};
  int CollisionRebuildQueueHardLimit{4096};
  int LiquidUpdateRadiusChunks{2};
  int FallingScanRadiusChunks{2};
};

} // namespace cutum

#endif // PHYSICSPROFILE_H
