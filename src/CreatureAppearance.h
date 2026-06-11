#ifndef CREATUREAPPEARANCE_H
#define CREATUREAPPEARANCE_H

#include <string>
#include "CreatureCatalogTypes.h"

namespace cutum {

class CreatureDefinitionStorage;
class SkinDefinitionStorage;

ResolvedCreatureAppearance ResolveCreatureAppearance(
    const CreatureDefinitionStorage& species,
    const SkinDefinitionStorage& skins,
    const std::string& speciesId,
    const std::string& skinId);

} // namespace cutum

#endif
