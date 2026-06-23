#ifndef AERIALPOSEPRESENTER_H
#define AERIALPOSEPRESENTER_H

#include "Pose/ICreaturePosePresenter.h"

namespace cutum
{

class UAerialPosePresenter : public ICreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def,
                             float dt) override;
};

} // namespace cutum

#endif
