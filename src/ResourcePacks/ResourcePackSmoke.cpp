#include "ResourcePacks/ResourcePackSmoke.h"

#include "App/Platform/IPlatformPaths.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "ResourcePacks/TextureOverrides.h"
#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/WorldGenRefs.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <iostream>
#include <vector>

namespace cutum
{

namespace
{

namespace fs = std::filesystem;

bool LoadRefs(const fs::path &assetRoot, const fs::path &writableRoot)
{
  const fs::path candidates[] = {writableRoot / "content" / "worldgen_refs.json",
                                 assetRoot / "content" / "worldgen_refs.json",
                                 fs::path("content") / "worldgen_refs.json"};
  for (const auto &path : candidates)
  {
    if (UWorldGenRefs::LoadFromFile(path))
    {
      return true;
    }
  }
  std::cerr << "ResourcePackSmoke: worldgen_refs.json not found" << std::endl;
  return false;
}

bool SmokeSelection(const ResourcePackSelection &selection,
                    const fs::path &assetRoot, const fs::path &writableRoot,
                    UBlockMergeRegistry &registry,
                    std::shared_ptr<UPlaceholderTextureCache> placeholders)
{
  UResourcePackResolver resolver;
  const auto packs = resolver.Resolve(selection, assetRoot, writableRoot);
  if (packs.empty())
  {
    std::cerr << "ResourcePackSmoke: no packs resolved" << std::endl;
    return false;
  }
  registry.SetPrimaryPackIds(selection.Primary);
  registry.SetWorldgenOwnerPackId(selection.WorldgenOwner);
  registry.Rebuild(packs, placeholders, 16);
  const size_t firstCount = registry.GetCubeDescriptors().size();
  if (firstCount == 0)
  {
    std::cerr << "ResourcePackSmoke: zero blocks after merge" << std::endl;
    return false;
  }
  registry.Rebuild(packs, placeholders, 16);
  if (registry.GetCubeDescriptors().size() < firstCount)
  {
    std::cerr << "ResourcePackSmoke: block count dropped after hot-reload"
              << std::endl;
    return false;
  }
  static const char *kTierA[] = {
      "bedrock", "stone",  "dirt",   "grass",   "sand",    "sandstone",
      "gravel",  "snow",   "clay",   "ice",     "hellrock",  "water",
      "lava",    "fire",   "wood",   "tree_log", "tree_leaves"};
  for (const char *name : kTierA)
  {
    if (registry.ResolveBlockName(name) == BLOCK_AIR)
    {
      std::cerr << "ResourcePackSmoke: missing tier A block '" << name << "'"
                << std::endl;
      return false;
    }
  }
  for (const auto &pack : packs)
  {
    const auto overrides = LoadTextureOverrides(pack.Root);
    if (!overrides.empty())
    {
      std::cout << "ResourcePackSmoke: loaded " << overrides.size()
                << " texture override(s) from " << pack.Id << std::endl;
    }
  }
  std::cout << "ResourcePackSmoke: OK " << packs.size() << " pack(s), "
            << firstCount << " blocks" << std::endl;
  return true;
}

} // namespace

int RunResourcePackSmoke(IPlatformPaths &paths)
{
  const fs::path assetRoot = paths.AssetRoot();
  const fs::path writableRoot = paths.WritableRoot();
  if (!LoadRefs(assetRoot, writableRoot))
  {
    return 1;
  }

  auto placeholders = std::make_shared<UPlaceholderTextureCache>(
      writableRoot / ".placeholder_cache", 16, glm::vec3(0.42f, 0.29f, 0.62f));
  UBlockMergeRegistry registry;

  ResourcePackSelection kenney;
  kenney.Primary = {"kenney_voxel_16"};
  kenney.Secondary = {"cubatarium_cc0_base"};
  kenney.WorldgenOwner = "kenney_voxel_16";
  if (!SmokeSelection(kenney, assetRoot, writableRoot, registry, placeholders))
  {
    return 1;
  }

  ResourcePackSelection minetest;
  minetest.Primary = {"minetest_default_16"};
  minetest.WorldgenOwner = "minetest_default_16";
  if (!SmokeSelection(minetest, assetRoot, writableRoot, registry, placeholders))
  {
    return 1;
  }

  ResourcePackSelection oga;
  oga.Primary = {"kenney_voxel_16"};
  oga.Secondary = {"oga_mc_inspired_16"};
  oga.WorldgenOwner = "kenney_voxel_16";
  const fs::path ogaPack = assetRoot / "resource_packs" / "oga_mc_inspired_16";
  const auto ogaOverrides = LoadTextureOverrides(ogaPack);
  if (ogaOverrides.find("grass") == ogaOverrides.end())
  {
    std::cerr << "ResourcePackSmoke: oga texture_overrides grass missing"
              << std::endl;
    return 1;
  }
  if (!SmokeSelection(oga, assetRoot, writableRoot, registry, placeholders))
  {
    return 1;
  }

  std::cout << "ResourcePackSmoke: all scenarios passed" << std::endl;
  return 0;
}

} // namespace cutum
