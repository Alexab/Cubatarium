#include "ResourcePacks/CreaturePackMerge.h"

#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"

#include <filesystem>

namespace cutum
{

void ApplyCreaturePackOverlays(UCreatureDefinitionStorage &defs,
                               UCreatureTextureStorage &textures,
                               USkinDefinitionStorage *skins,
                               const std::filesystem::path &baseCreaturesRoot,
                               const std::filesystem::path &baseSkinsRoot,
                               const std::vector<ResourcePackManifest> &packs)
{
  defs.Load(baseCreaturesRoot.string());
  for (const auto &pack : packs)
  {
    const std::filesystem::path creaturesDir = pack.Root / "creatures";
    if (std::filesystem::exists(creaturesDir))
    {
      defs.LoadOverlay(creaturesDir.string());
    }
  }

  if (skins)
  {
    skins->Load(baseSkinsRoot.string());
    for (const auto &pack : packs)
    {
      const std::filesystem::path skinsDir = pack.Root / "skins";
      if (std::filesystem::exists(skinsDir))
      {
        skins->LoadOverlay(skinsDir.string());
      }
    }
  }

  textures.LoadFromCreatureAndSkinRoots(baseCreaturesRoot.string(),
                                        baseSkinsRoot.string());
  for (const auto &pack : packs)
  {
    const std::filesystem::path creaturesDir = pack.Root / "creatures";
    if (std::filesystem::exists(creaturesDir))
    {
      textures.MergeCreatureRoot(creaturesDir.string());
    }
    const std::filesystem::path skinsDir = pack.Root / "skins";
    if (std::filesystem::exists(skinsDir))
    {
      textures.MergeSkinRoot(skinsDir.string());
    }
  }
}

} // namespace cutum
