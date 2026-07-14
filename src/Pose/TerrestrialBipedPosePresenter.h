#ifndef TERRESTRIALBIPEDPOSEPRESENTER_H
#define TERRESTRIALBIPEDPOSEPRESENTER_H

#include "Pose/IUCreaturePosePresenter.h"

namespace cutum
{

class UTerrestrialBipedPosePresenter : public IUCreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def, float dt) override;
};

} // namespace cutum

#endif
