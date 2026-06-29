#include "Pose/Skeletal/SkeletalAnimationProfiles.h"

namespace cutum
{

std::string SkeletalAnimationProfiles::ResolveProfileId(const CreatureDefinition &def)
{
  if (!def.visual.skeletal.animationProfile.empty())
  {
    return def.visual.skeletal.animationProfile;
  }
  switch (def.locomotionArchetype)
  {
  case LocomotionArchetype::TerrestrialQuadruped:
    return "quadruped";
  case LocomotionArchetype::TerrestrialBiped:
    return "humanoid";
  case LocomotionArchetype::Aerial:
    return def.Id == "chicken" ? "chicken" : "aerial";
  case LocomotionArchetype::Aquatic:
    return "aquatic";
  case LocomotionArchetype::Serpentine:
    return "serpentine";
  default:
    return "quadruped";
  }
}

} // namespace cutum
