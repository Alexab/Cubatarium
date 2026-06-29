#ifndef SKELETAL_BONE_POSE_ENGINE_H
#define SKELETAL_BONE_POSE_ENGINE_H

#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"

namespace cutum
{

class SkeletalBonePoseEngine
{
public:
  static SkeletalCreaturePose Compute(const CreatureLocomotionFacts &facts,
                                     const CreatureDefinition &def, float dt);
};

} // namespace cutum

#endif
