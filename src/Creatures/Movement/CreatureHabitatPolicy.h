#ifndef CREATUREHABITATPOLICY_H
#define CREATUREHABITATPOLICY_H

#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Locomotion/LocomotionTypes.h"

namespace cutum
{

enum class HabitatContext
{
  Spawn,
  WanderCurrent,
  WanderTarget,
  MoveApply,
};

bool TerrestrialCanWalkOn(const EnvironmentSample &env);
bool HabitatAllows(CreatureHabitat habitat, HabitatContext ctx,
                   const EnvironmentSample &env);

} // namespace cutum

#endif
