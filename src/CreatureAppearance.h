#ifndef CREATUREAPPEARANCE_H
#define CREATUREAPPEARANCE_H

#include "CreatureCatalogTypes.h"
#include <string>

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;

ResolvedCreatureAppearance
ResolveCreatureAppearance(const UCreatureDefinitionStorage &species,
                          const USkinDefinitionStorage &skins,
                          const std::string &speciesId,
                          const std::string &skinId);

} // namespace cutum

#endif
