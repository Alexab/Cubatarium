#ifndef CREATUREWANDERBEHAVIOR_H
#define CREATUREWANDERBEHAVIOR_H

#include "CreatureCatalogTypes.h"
#include "LocomotionTypes.h"

namespace cutum {

class Creature;

void ApplyWanderIntent(Creature& self, const CreatureBehaviorParams& params,
                       const CreatureLocomotionCapabilities& locomotion, float dt);

} // namespace cutum

#endif
