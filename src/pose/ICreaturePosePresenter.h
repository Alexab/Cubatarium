#ifndef ICREATUREPOSEPRESENTER_H
#define ICREATUREPOSEPRESENTER_H

#include "CreatureLocomotionFacts.h"
#include "CreaturePoseParams.h"
#include "LocomotionTypes.h"

namespace cutum {

struct CreatureDefinition;

class ICreaturePosePresenter {
 public:
 virtual ~ICreaturePosePresenter() = default;
 virtual LocomotionArchetype GetArchetype() const = 0;
 virtual CreaturePoseParams Compute(const CreatureLocomotionFacts& facts,
                                    const CreatureDefinition& def, float dt) = 0;
};

} // namespace cutum

#endif
