#ifndef LOCOMOTIONSTATEDERIVE_H
#define LOCOMOTIONSTATEDERIVE_H

#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"

namespace cutum
{

bool IsAirborneLocomotionState(LocomotionState state);
bool IsAquaticLocomotionState(LocomotionState state);

LocomotionState
DeriveLocomotionState(LocomotionArchetype archetype,
                      const CreatureLocomotionFacts &raw,
                      const CreatureLocomotionCapabilities &caps,
                      const CreatureLocomotionRawInput *hintInput = nullptr);

} // namespace cutum

#endif
