#include "CreatureAppearance.h"
#include "CreatureDefinitionStorage.h"
#include "SkinDefinitionStorage.h"

namespace cutum {

ResolvedCreatureAppearance ResolveCreatureAppearance(
    const CreatureDefinitionStorage& species,
    const SkinDefinitionStorage& skins,
    const std::string& speciesId,
    const std::string& skinId)
{
 ResolvedCreatureAppearance result;
 const CreatureDefinition* def = species.Get(speciesId);
 if (def) {
  result.wireframeColor = def->visual.wireframeColor;
  result.visualBackend = def->visual.backend;
 }
 if (!skinId.empty()) {
  if (const SkinDefinition* skin = skins.Get(skinId)) {
   if (skin->creatureId.empty() || skin->creatureId == speciesId) {
    result.wireframeColor = skin->wireframeTint;
   }
  }
 }
 return result;
}

} // namespace cutum
