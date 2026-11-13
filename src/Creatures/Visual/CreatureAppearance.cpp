#include "Creatures/Visual/CreatureAppearance.h"
#include <filesystem>
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"

namespace cutum
{

namespace
{

std::string ResolvePartTextureKey(const std::string &speciesId,
                                  const std::string &stem,
                                  const ResolvedCreatureAppearance &appearance)
{
  if (const auto mapped = appearance.skinTextureOverrides.find(stem);
      mapped != appearance.skinTextureOverrides.end())
  {
    return mapped->second;
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
  part.baseTextureAssetKey = def.Id + "/" + stem;
  part.textureAssetKey = ResolvePartTextureKey(def.Id, stem, result);
  result.Parts.push_back(part);
}

void ResolvePartsFromSpecies(const CreatureDefinition &def,
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
    part.baseTextureAssetKey = def.Id + "/" + (stem.empty() ? "body" : stem);
    part.textureAssetKey = ResolvePartTextureKey(def.Id, stem, result);
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
                          const std::string &skinId, const HumanAppearance &human)
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
      result.effectiveSkinId = skinId;
      result.wireframeColor = skin->wireframeTint;
      const std::string prefix = "skin/" + skinId + "/";
      for (const auto &mapped : skin->textureMap)
        result.skinTextureOverrides[mapped.first] = prefix + mapped.second;
      if (!skin->textureKey.empty())
      {
        result.skinTextureOverrides.emplace(skin->textureKey, prefix + skin->textureKey);
        result.skinTextureOverrides.emplace("", prefix + skin->textureKey);
      }
      // Bundled B skins have one whole-mesh diffuse atlas as well as the
      // legacy rigid-part files. Explicit diffuse mappings take precedence.
      // Do not spread this atlas over unrelated glTF/rigid materials.
      if (ParseCreatureVisualBackend(def->visual.backend) == CreatureVisualBackend::BoneSkeleton)
        result.skinTextureOverrides.emplace("diffuse", prefix + "diffuse");
    }
    else
    {
      skin = nullptr;
    }
  }

  if (ParseCreatureVisualBackend(def->visual.backend) ==
      CreatureVisualBackend::RigidVoxels)
  {
    ResolvePartsFromSpecies(*def, result);
  }
  if (speciesId == "human" && def->visual.humanAppearanceEnabled && (!skin || skin->humanAppearanceEnabled)) {
    auto look=human;look.Normalize();result.appearanceKey=look.Key();
    const std::string geometry="appearance/"+look.bodyVariant+"_"+look.hairstyleId+".geo.json";
    const std::string stem="appearance_"+look.bodyVariant+"_"+look.hairColorId;
    const auto root=std::filesystem::path(def->visual.sourceDirectory);
    const auto textureRoot=skin ? root.parent_path().parent_path()/"skins"/result.effectiveSkinId : root;
    // Custom packs without the new matched geometry/atlas pair retain their base appearance.
    if (std::filesystem::is_regular_file(root/geometry) &&
        std::filesystem::is_regular_file(textureRoot/"textures"/(stem+".png"))) {
      result.geometryFileOverride=geometry;
      result.skinTextureOverrides["diffuse"]=skin ? "skin/"+result.effectiveSkinId+"/"+stem : "human/"+stem;
    }
  }
  return result;
}

} // namespace cutum
