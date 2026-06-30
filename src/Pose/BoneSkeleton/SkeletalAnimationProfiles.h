#ifndef SKELETAL_ANIMATION_PROFILES_H
#define SKELETAL_ANIMATION_PROFILES_H

#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include <string>

namespace cutum
{

class SkeletalAnimationProfiles
{
public:
  static std::string ResolveProfileId(const CreatureDefinition &def);
};

} // namespace cutum

#endif
