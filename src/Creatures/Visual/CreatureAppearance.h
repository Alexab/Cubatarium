#ifndef CREATUREAPPEARANCE_H
#define CREATUREAPPEARANCE_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Core/HumanAppearance.h"
#include <string>

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;

ResolvedCreatureAppearance
ResolveCreatureAppearance(const UCreatureDefinitionStorage &species,
                          const USkinDefinitionStorage &skins,
                          const std::string &speciesId,
                          const std::string &skinId,
                          const HumanAppearance &human = {});

} // namespace cutum

#endif
