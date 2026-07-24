#ifndef CREATUREVISUALFACTORY_H
#define CREATUREVISUALFACTORY_H

#include "Creatures/Visual/CreatureVisual.h"
#include <memory>

namespace cutum
{

struct CreatureDefinition;
std::unique_ptr<IUCreatureVisual>
CreateCreatureVisual(const CreatureDefinition &def);

} // namespace cutum

#endif
