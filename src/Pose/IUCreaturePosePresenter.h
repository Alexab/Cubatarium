#ifndef ICREATUREPOSEPRESENTER_H
#define ICREATUREPOSEPRESENTER_H

#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Pose/CreaturePoseParams.h"

namespace cutum
{

struct CreatureDefinition;

class IUCreaturePosePresenter
{
public:
  virtual ~IUCreaturePosePresenter() = default;
  virtual LocomotionArchetype GetArchetype() const = 0;
  virtual CreaturePoseParams Compute(const CreatureLocomotionFacts &facts,
                                     const CreatureDefinition &def,
                                     float dt) = 0;
};

} // namespace cutum

#endif
