#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"

namespace cutum
{

namespace
{

std::string ResolvePartTextureKey(const std::string &speciesId,
                                  const std::string &stem,
                                  const SkinDefinition *skin,
                                  const std::string &skinId)
{
  if (skin)
  {
    const auto mapped = skin->textureMap.find(stem);
    if (mapped != skin->textureMap.end())
    {
      return "skin/" + skinId + "/" + mapped->second;
    }
    if (stem == skin->textureKey || stem.empty())
    {
      return "skin/" + skinId + "/" + skin->textureKey;
    }
  }
  if (stem.empty())
  {
    return speciesId + "/body";
  }
  return speciesId + "/" + stem;
}

void AppendFallbackPart(const CreatureDefinition &def,
                        ResolvedCreatureAppearance &result)
{
  ResolvedCreaturePart part;
  part.partId = "body";
  part.offsetBlocks = glm::vec3(0.0f, def.bounds.restSizeBlocks.y * 0.5f, 0.0f);
  part.sizeBlocks = def.bounds.restSizeBlocks;
  const std::string stem = def.visual.defaultTextureKey.empty()
                               ? "body"
                               : def.visual.defaultTextureKey;
  part.textureAssetKey = def.Id + "/" + stem;
  result.Parts.push_back(part);
}

void ResolvePartsFromSpecies(const CreatureDefinition &def,
                             const SkinDefinition *skin,
                             const std::string &skinId,
                             ResolvedCreatureAppearance &result)
{
  if (def.visual.Parts.empty())
  {
    AppendFallbackPart(def, result);
    return;
  }
  for (const CreatureVisualPartDef &partDef : def.visual.Parts)
  {
    ResolvedCreaturePart part;
    part.partId = partDef.Id;
    part.offsetBlocks = partDef.offsetBlocks;
    part.sizeBlocks = partDef.sizeBlocks;
    const std::string stem = partDef.textureStem.empty()
                                 ? def.visual.defaultTextureKey
                                 : partDef.textureStem;
    part.textureAssetKey = ResolvePartTextureKey(def.Id, stem, skin, skinId);
    part.PivotBlocks = partDef.PivotBlocks;
    part.HasPivot = partDef.HasPivot;
    part.LimbKind = partDef.LimbKind;
    part.LimbAxis = partDef.LimbAxis;
    result.Parts.push_back(part);
  }
}

} // namespace

ResolvedCreatureAppearance
ResolveCreatureAppearance(const UCreatureDefinitionStorage &species,
                          const USkinDefinitionStorage &skins,
                          const std::string &speciesId,
                          const std::string &skinId)
{
  ResolvedCreatureAppearance result;
  const CreatureDefinition *def = species.Get(speciesId);
  if (!def)
  {
    result.useWireframeFallback = true;
    return result;
  }
  result.wireframeColor = def->visual.wireframeColor;
  result.visualBackend = def->visual.backend;
  result.textureLayout = def->visual.textureLayout;
  result.defaultTextureKey = def->visual.defaultTextureKey.empty()
                                 ? "body"
                                 : def->visual.defaultTextureKey;

  const SkinDefinition *skin = nullptr;
  if (!skinId.empty())
  {
    skin = skins.Get(skinId);
    if (skin && (skin->creatureId.empty() || skin->creatureId == speciesId))
    {
      result.wireframeColor = skin->wireframeTint;
    }
    else
    {
      skin = nullptr;
    }
  }

  if (ParseCreatureVisualBackend(def->visual.backend) ==
      CreatureVisualBackend::RigidVoxels)
  {
    ResolvePartsFromSpecies(*def, skin, skinId, result);
  }
  return result;
}

} // namespace cutum
