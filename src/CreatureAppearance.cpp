#include "CreatureAppearance.h"
#include "CreatureDefinitionStorage.h"
#include "SkinDefinitionStorage.h"

namespace cutum {

namespace {

std::string ResolvePartTextureKey(const std::string& speciesId, const std::string& stem,
                                  const SkinDefinition* skin, const std::string& skinId)
{
 if (skin) {
  const auto mapped = skin->textureMap.find(stem);
  if (mapped != skin->textureMap.end()) {
   return "skin/" + skinId + "/" + mapped->second;
  }
  if (stem == skin->textureKey || stem.empty()) {
   return "skin/" + skinId + "/" + skin->textureKey;
  }
 }
 if (stem.empty()) {
  return speciesId + "/body";
 }
 return speciesId + "/" + stem;
}

void AppendFallbackPart(const CreatureDefinition& def, ResolvedCreatureAppearance& result)
{
 ResolvedCreaturePart part;
 part.partId = "body";
 part.offsetBlocks = glm::vec3(0.0f, def.bounds.restSizeBlocks.y * 0.5f, 0.0f);
 part.sizeBlocks = def.bounds.restSizeBlocks;
 const std::string stem = def.visual.defaultTextureKey.empty() ? "body" : def.visual.defaultTextureKey;
 part.textureAssetKey = def.id + "/" + stem;
 result.parts.push_back(part);
}

void ResolvePartsFromSpecies(const CreatureDefinition& def, const SkinDefinition* skin,
                             const std::string& skinId, ResolvedCreatureAppearance& result)
{
 if (def.visual.parts.empty()) {
  AppendFallbackPart(def, result);
  return;
 }
 for (const CreatureVisualPartDef& partDef : def.visual.parts) {
  ResolvedCreaturePart part;
  part.partId = partDef.id;
  part.offsetBlocks = partDef.offsetBlocks;
  part.sizeBlocks = partDef.sizeBlocks;
  const std::string stem =
      partDef.textureStem.empty() ? def.visual.defaultTextureKey : partDef.textureStem;
  part.textureAssetKey = ResolvePartTextureKey(def.id, stem, skin, skinId);
  result.parts.push_back(part);
 }
}

} // namespace

ResolvedCreatureAppearance ResolveCreatureAppearance(
    const CreatureDefinitionStorage& species,
    const SkinDefinitionStorage& skins,
    const std::string& speciesId,
    const std::string& skinId)
{
 ResolvedCreatureAppearance result;
 const CreatureDefinition* def = species.Get(speciesId);
 if (!def) {
  result.useWireframeFallback = true;
  return result;
 }
 result.wireframeColor = def->visual.wireframeColor;
 result.visualBackend = def->visual.backend;

 const SkinDefinition* skin = nullptr;
 if (!skinId.empty()) {
  skin = skins.Get(skinId);
  if (skin && (skin->creatureId.empty() || skin->creatureId == speciesId)) {
   result.wireframeColor = skin->wireframeTint;
  } else {
   skin = nullptr;
  }
 }

 ResolvePartsFromSpecies(*def, skin, skinId, result);
 return result;
}

} // namespace cutum
