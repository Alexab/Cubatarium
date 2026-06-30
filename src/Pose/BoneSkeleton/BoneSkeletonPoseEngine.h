#ifndef BONE_SKELETON_POSE_ENGINE_H
#define BONE_SKELETON_POSE_ENGINE_H

#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"

namespace cutum
{

class BoneSkeletonPoseEngine
{
public:
  static BoneSkeletonPose Compute(const CreatureLocomotionFacts &facts,
                                     const CreatureDefinition &def, float dt);
};

} // namespace cutum

#endif
