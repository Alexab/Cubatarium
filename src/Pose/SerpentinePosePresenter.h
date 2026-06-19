#ifndef SERPENTINEPOSEPRESENTER_H
#define SERPENTINEPOSEPRESENTER_H

#include "Pose/ICreaturePosePresenter.h"

namespace cutum
{

class USerpentinePosePresenter : public ICreaturePosePresenter
{
public:
  LocomotionArchetype GetArchetype() const override;
  CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                             const CreatureDefinition &def,
                             float dt) override;
};

} // namespace cutum

#endif
