#ifndef CREATUREVISUALFACTORY_H
#define CREATUREVISUALFACTORY_H

#include "CreatureVisual.h"
#include <memory>

namespace cutum
{

struct CreatureDefinition;
std::unique_ptr<ICreatureVisual>
CreateCreatureVisual(const CreatureDefinition &def);

} // namespace cutum

#endif
