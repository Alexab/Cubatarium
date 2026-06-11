#ifndef CREATUREVISUALFACTORY_H
#define CREATUREVISUALFACTORY_H

#include <memory>
#include "CreatureVisual.h"

namespace cutum {

struct CreatureDefinition;
std::unique_ptr<ICreatureVisual> CreateCreatureVisual(const CreatureDefinition& def);

} // namespace cutum

#endif
