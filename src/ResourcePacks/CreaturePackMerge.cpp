#include "ResourcePacks/CreaturePackMerge.h"

#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Creatures/Visual/CreatureMeshGpuCache.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"

#include <filesystem>

namespace cutum
{

bool ApplyCreaturePackOverlays(UCreatureDefinitionStorage &defs,
                               UCreatureTextureStorage &textures,
                               USkinDefinitionStorage *skins,
                               const std::filesystem::path &baseCreaturesRoot,
                               const std::filesystem::path &baseSkinsRoot,
                               const std::vector<ResourcePackManifest> &packs)
{
  std::vector<std::filesystem::path> overlays;
  overlays.reserve(packs.size());
  for (const auto &pack : packs)
    overlays.push_back(pack.Root);
  UCreatureTextureStorage candidate;
  struct RetireTextures
  {
    UCreatureTextureStorage &Storage;
    ~RetireTextures() { Storage.Clear(); }
  } retire{candidate};
  if (!candidate.LoadFromCreatureAndSkinRoots(baseCreaturesRoot.string(), baseSkinsRoot.string(), overlays))
    return false;

  // Invalidate before parsing: rigid parts are read into definitions during
  // Load, and retained B/G visuals must observe the same content generation.
  CreatureRigidModelCache::Instance().Clear();
  CreatureBoneSkeletonCache::Instance().SetCreaturesRoot(baseCreaturesRoot.string());
  CreatureGltfCache::Instance().SetCreaturesRoot(baseCreaturesRoot.string());
  defs.Load(baseCreaturesRoot.string());
  // Pack overlays can replace models: their content hashes differ, but the
  // replaced entries would stay cached until engine shutdown — clear the
  // GPU mesh cache alongside the rigid cache (audit V8).
  CreatureMeshGpuCache::Instance().DestroyAll();
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

  textures.SwapContents(candidate);
  return true;
}

} // namespace cutum
