#ifndef TERRESTRIALBIPEDPOSEPRESENTER_H
#define TERRESTRIALBIPEDPOSEPRESENTER_H

#include "ICreaturePosePresenter.h"

namespace cutum
{

class UTerrestrialBipedPosePresenter : public ICreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def, float dt) override;
};

} // namespace cutum

#endif
