#ifndef AERIALPOSEPRESENTER_H
#define AERIALPOSEPRESENTER_H

#include "Pose/IUCreaturePosePresenter.h"

namespace cutum
{

class UAerialPosePresenter : public IUCreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def,
                             float dt) override;
};

} // namespace cutum

#endif
