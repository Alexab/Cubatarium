#include "ResourcePacks/CreaturePackMerge.h"

#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"

#include <filesystem>

namespace cutum
{

void ApplyCreaturePackOverlays(UCreatureDefinitionStorage &defs,
                               UCreatureTextureStorage &textures,
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

  textures.LoadFromCreatureAndSkinRoots(baseCreaturesRoot.string(),
                                        baseSkinsRoot.string());
  for (const auto &pack : packs)
  {
    const std::filesystem::path creaturesDir = pack.Root / "creatures";
    if (std::filesystem::exists(creaturesDir))
    {
      textures.MergeCreatureRoot(creaturesDir.string());
    }
  }
}

} // namespace cutum
