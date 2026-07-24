#ifndef TERRESTRIALQUADRUPEDPOSEPRESENTER_H
#define TERRESTRIALQUADRUPEDPOSEPRESENTER_H

#include "Pose/IUCreaturePosePresenter.h"

namespace cutum
{

class UTerrestrialQuadrupedPosePresenter : public IUCreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                               const CreatureDefinition &def,
                               float dt) override;
};

} // namespace cutum

#endif
