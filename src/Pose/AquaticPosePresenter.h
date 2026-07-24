#ifndef AQUATICPOSEPRESENTER_H
#define AQUATICPOSEPRESENTER_H

#include "Pose/IUCreaturePosePresenter.h"

namespace cutum
{

class UAquaticPosePresenter : public IUCreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def,
                             float dt) override;
};

} // namespace cutum

#endif
