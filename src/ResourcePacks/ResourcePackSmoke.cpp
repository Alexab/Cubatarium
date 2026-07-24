#include "ResourcePacks/ResourcePackSmoke.h"

#include "App/Platform/IUPlatformPaths.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "ResourcePacks/TextureOverrides.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/WorldGenRefs.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>

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
      "lava",    "fire",   "wood",   "tree_log", "tree_bark", "tree_leaves"};
  for (const char *name : kTierA)
  {
    if (registry.ResolveBlockName(name) == BLOCK_AIR)
    {
      std::cerr << "ResourcePackSmoke: missing tier A block '" << name << "'"
                << std::endl;
      return false;
    }
  }
  UBlockDefinitionStorage defs;
  registry.PopulateBlockDefinitionStorage(defs);
  for (const char *fluid : {"water", "lava", "fire"})
  {
    const BlockDefinition *def = defs.GetByName(fluid);
    if (!def)
    {
      std::cerr << "ResourcePackSmoke: missing definition for '" << fluid << "'"
                << std::endl;
      return false;
    }
    if (def->Physics.Movement.Occupancy >= 1.0f)
    {
      std::cerr << "ResourcePackSmoke: block '" << fluid
                << "' is solid (occupancy=" << def->Physics.Movement.Occupancy
                << ")" << std::endl;
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
  static const char *kDecorStems[] = {"stone", "tree_bark"};
  const bool minetestPrimary =
      std::find(selection.Primary.begin(), selection.Primary.end(),
                "minetest_default_16") != selection.Primary.end();
  if (minetestPrimary)
  {
    for (const char *name : kDecorStems)
    {
      const BlockId id = registry.ResolveBlockName(name);
      if (id == BLOCK_AIR)
      {
        std::cerr << "ResourcePackSmoke: missing decor block '" << name << "'"
                  << std::endl;
        return false;
      }
      bool found = false;
      for (const MergedCubeDesc &desc : registry.GetCubeDescriptors())
      {
        if (desc.Name != name)
        {
          continue;
        }
        found = true;
        for (const std::string &stem : desc.Stems)
        {
          if (stem.rfind("__ph_", 0) == 0)
          {
            std::cerr << "ResourcePackSmoke: placeholder texture stem for '"
                      << name << "': " << stem << std::endl;
            return false;
          }
        }
        break;
      }
      if (!found)
      {
        std::cerr << "ResourcePackSmoke: no cube descriptor for '" << name
                  << "'" << std::endl;
        return false;
      }
    }
  }

  std::cout << "ResourcePackSmoke: OK " << packs.size() << " pack(s), "
            << firstCount << " blocks" << std::endl;
  return true;
}

bool SmokeStartupInit(const fs::path &assetRoot, const fs::path &writableRoot,
                    std::shared_ptr<UPlaceholderTextureCache> placeholders)
{
  auto registry = std::make_shared<UBlockMergeRegistry>();
  const ResourcePackSelection defaults = DefaultResourcePackSelection();
  if (!SmokeSelection(defaults, assetRoot, writableRoot, *registry,
                      placeholders))
  {
    return false;
  }

  UResourcePackResolver resolver;
  const auto packs = resolver.Resolve(defaults, assetRoot, writableRoot);
  UBlockRegistry blockRegistry(nullptr, nullptr);
  blockRegistry.SetMergeRegistry(registry);

  UObjectLibrary objects;
  objects.LoadMerged(assetRoot / "objects", packs, blockRegistry);

  // Simulate worldgen slot resolution creating synthetic blocks (e.g. ore_coal).
  (void)blockRegistry.GetIdByTypeName("ore_coal");
  (void)blockRegistry.GetIdByTypeName("ore_iron");

  UBlockDefinitionStorage defsAfterObjects;
  registry->PopulateBlockDefinitionStorage(defsAfterObjects);
  for (const char *fluid : {"water", "lava", "fire"})
  {
    const BlockId id = registry->ResolveBlockName(fluid);
    const BlockDefinition *def = defsAfterObjects.GetById(id);
    const BlockDefinition *defByName = defsAfterObjects.GetByName(fluid);
    if (!def || !defByName || def != defByName ||
        def->Physics.Movement.Occupancy >= 1.0f)
    {
      std::cerr << "ResourcePackSmoke: post-worldgen fluid '" << fluid
                << "' invalid (id=" << id << ")" << std::endl;
      return false;
    }
  }

  const size_t objectCount = objects.ListNames().size();
  constexpr size_t kMinObjects = 45;
  if (objectCount < kMinObjects)
  {
    std::cerr << "ResourcePackSmoke: startup init loaded only " << objectCount
              << " object(s), expected >= " << kMinObjects << std::endl;
    return false;
  }

  const BlockId stone_id = blockRegistry.GetIdByTypeName("stone");
  const BlockId tree_bark_id = blockRegistry.GetIdByTypeName("tree_bark");
  if (stone_id == BLOCK_AIR || tree_bark_id == BLOCK_AIR)
  {
    std::cerr << "ResourcePackSmoke: stone/tree_bark not resolved after object load"
              << std::endl;
    return false;
  }
  const auto *path = objects.Get("path_cobble_3x3");
  const auto *log = objects.Get("deco_log_pine");
  if (!path || !log || path->voxels.empty() || log->voxels.empty())
  {
    std::cerr << "ResourcePackSmoke: missing path_cobble_3x3 or deco_log_pine"
              << std::endl;
    return false;
  }
  bool path_has_stone = false;
  for (const auto &voxel : path->voxels)
  {
    if (voxel.Id == stone_id)
    {
      path_has_stone = true;
      break;
    }
  }
  bool log_has_bark = false;
  for (const auto &voxel : log->voxels)
  {
    if (voxel.Id == tree_bark_id)
    {
      log_has_bark = true;
      break;
    }
  }
  if (!path_has_stone || !log_has_bark)
  {
    std::cerr << "ResourcePackSmoke: prefab blocks not bound (stone="
              << path_has_stone << ", tree_bark=" << log_has_bark << ")"
              << std::endl;
    return false;
  }

  auto defs_storage = std::make_shared<UBlockDefinitionStorage>();
  registry->PopulateBlockDefinitionStorage(*defs_storage);
  blockRegistry.SetDefinitions(defs_storage);
  if (!blockRegistry.BlocksMovement(tree_bark_id) ||
      !blockRegistry.BlocksMovement(stone_id))
  {
    std::cerr << "ResourcePackSmoke: decor blocks are not movement-solid"
              << std::endl;
    return false;
  }
  if (!objects.ValidateCriticalPrefabs())
  {
    return false;
  }

  UBlockWorld world;
  for (int y = 0; y <= 47; ++y)
  {
    world.SetBlock(glm::ivec3(0, y, 0), stone_id);
  }
  world.SetBlock(glm::ivec3(0, 48, 0), stone_id);
  world.SetBlock(glm::ivec3(0, 49, 0), stone_id);
  world.SetBlock(glm::ivec3(0, 50, 0), stone_id);
  world.SetBlock(glm::ivec3(0, 51, 0), stone_id);
  for (int dx = -1; dx <= 1; ++dx)
  {
    world.SetBlock(glm::ivec3(dx, 51, 0), stone_id);
  }
  const glm::ivec3 log_anchor(0, 52, 0);
  if (!CanPlaceObjectAtForWorldGen(world, blockRegistry, *log, log_anchor, 80,
                                   48))
  {
    std::cerr << "ResourcePackSmoke: deco_log_pine rejected by worldgen placement"
              << std::endl;
    return false;
  }
  const ObjectPlacementStats log_stats =
      PlaceObjectAt(world, blockRegistry, *log, log_anchor, false);
  if (log_stats.placedCount != static_cast<int>(log->voxels.size()))
  {
    std::cerr << "ResourcePackSmoke: deco_log_pine placed " << log_stats.placedCount
              << "/" << log->voxels.size() << " voxels" << std::endl;
    return false;
  }
  if (world.GetBlock(glm::ivec3(0, 52, 0)) != tree_bark_id)
  {
    std::cerr << "ResourcePackSmoke: deco_log center is not tree_bark"
              << std::endl;
    return false;
  }
  if (world.GetBlock(glm::ivec3(0, 51, 0)) != stone_id)
  {
    std::cerr << "ResourcePackSmoke: deco_log should rest above ground surface"
              << std::endl;
    return false;
  }

  std::cout << "ResourcePackSmoke: startup init OK (" << objectCount
            << " objects after pack merge)" << std::endl;
  return true;
}

} // namespace

int RunResourcePackSmoke(IUPlatformPaths &paths)
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

  if (!SmokeStartupInit(assetRoot, writableRoot, placeholders))
  {
    return 1;
  }

  std::cout << "ResourcePackSmoke: all scenarios passed" << std::endl;
  return 0;
}

} // namespace cutum
