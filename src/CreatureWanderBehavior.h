#ifndef CREATUREWANDERBEHAVIOR_H
#define CREATUREWANDERBEHAVIOR_H

#include "CreatureCatalogTypes.h"

namespace cutum {

class Creature;

void ApplyWanderIntent(Creature& self, const CreatureBehaviorParams& params, float dt);

} // namespace cutum

#endif
