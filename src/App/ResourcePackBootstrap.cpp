#include "App/ResourcePackBootstrap.h"

#include <iostream>
#include <unordered_set>

#include <glm/glm.hpp>

#include "App/Core.h"
#include "App/ResourcePackSelectionUtil.h"
#include "Core/ColorUtil.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/CreaturePackMerge.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"

namespace cutum
{

void UResourcePackBootstrap::InitPlaceholderCache(UCore &core)
{
  const glm::vec3 bg = ParseHexColor(core.ResourcePacks.PlaceholderBackground,
                                     glm::vec3(0.42f, 0.29f, 0.62f));
  core.PlaceholderCacheInstance = std::make_shared<UPlaceholderTextureCache>(
      core.ExeDir / ".placeholder_cache", core.ResourcePacks.PlaceholderTileSize,
      bg, static_cast<size_t>(core.ResourcePacks.PlaceholderCacheMaxEntries));
}

void UResourcePackBootstrap::RebuildBlockTexturesFromMergeRegistry(UCore &core)
{
  if (!core.BlockMergeRegistryInstance || !core.BlockDefinitionsInstance ||
      !core.TextureBaseStorageInstance || !core.TextureCubeStorageInstance)
  {
    return;
  }
  if (core.WorldInstance)
  {
    core.WorldInstance->WaitForPendingMeshJobs();
  }
  core.BlockMergeRegistryInstance->PopulateBlockDefinitionStorage(
      *core.BlockDefinitionsInstance);
  core.TextureBaseStorageInstance->Clear();
  core.BlockMergeRegistryInstance->PopulateTextureBaseStorage(
      *core.TextureBaseStorageInstance);
  core.TextureCubeStorageInstance->Clear();
  core.TextureCubeStorageInstance->SetBlockDefinitions(
      core.BlockDefinitionsInstance);
  core.TextureCubeStorageInstance->BuildFromDescriptors(
      core.BlockMergeRegistryInstance->GetCubeDescriptors());
}

bool UResourcePackBootstrap::RegisterRuntimeBlock(
    UCore &core, const BlockDefinition &def,
    const std::array<std::string, 6> &textureStems)
{
  if (!core.BlockMergeRegistryInstance || !core.WorldInstance)
  {
    return false;
  }
  const BlockId id =
      core.BlockMergeRegistryInstance->RegisterRuntimeBlock(def, textureStems);
  core.RuntimeBlockFlushPending = true;
  FlushRuntimeBlockOverlay(core);
  if (id == BLOCK_AIR &&
      core.BlockMergeRegistryInstance->GetNameToId().count(def.Name) == 0)
  {
    return false;
  }
  const BlockId resolved = core.BlockMergeRegistryInstance->ResolveName(def.Name);
  if (resolved == BLOCK_AIR)
  {
    return false;
  }
  return true;
}

void UResourcePackBootstrap::BeginRuntimeBlockBatch(UCore &core)
{
  ++core.RuntimeBlockBatchDepth;
}

void UResourcePackBootstrap::EndRuntimeBlockBatch(UCore &core)
{
  core.RuntimeBlockBatchDepth = std::max(0, core.RuntimeBlockBatchDepth - 1);
  FlushRuntimeBlockOverlay(core);
}

void UResourcePackBootstrap::FlushRuntimeBlockOverlay(UCore &core)
{
  if (!core.RuntimeBlockFlushPending || core.RuntimeBlockBatchDepth > 0 ||
      !core.BlockMergeRegistryInstance)
  {
    return;
  }
  core.BlockMergeRegistryInstance->FlushRuntimeOverlay();
  core.RuntimeBlockFlushPending = false;
  RebuildBlockTexturesFromMergeRegistry(core);
  if (core.WorldInstance)
  {
    core.WorldInstance->OnBlockRegistryRuntimeOverlayChanged();
  }
}

std::vector<std::string> UResourcePackBootstrap::NormalizeEnabledPackIds(
    const UCore &core, const std::vector<std::string> &requested) const
{
  const auto installed = core.ListInstalledResourcePacks();
  std::unordered_set<std::string> installedIds;
  for (const auto &pack : installed)
  {
    installedIds.insert(pack.Id);
  }
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  for (const auto &id : requested)
  {
    if (seen.count(id) != 0)
    {
      continue;
    }
    if (installedIds.count(id) != 0)
    {
      seen.insert(id);
      result.push_back(id);
    }
    else
    {
      std::cerr << "UCore: resource pack not installed, skipping: " << id
                << std::endl;
    }
  }
  return result;
}

bool UResourcePackBootstrap::ApplyResourcePacks(
    UCore &core, const std::vector<std::string> &enabledIds)
{
  ResourcePackSelection selection;
  selection.Primary = enabledIds;
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  return ApplyResourcePacks(core, selection);
}

bool UResourcePackBootstrap::ApplyResourcePacks(UCore &core,
                                                const ResourcePackSelection &selectionIn)
{
  if (!core.BlockMergeRegistryInstance)
  {
    return false;
  }
  ResourcePackSelection selection =
      NormalizeResourcePackSelection(core, *this, selectionIn);
  if (selection.Primary.empty())
  {
    std::cerr << "UCore::ApplyResourcePacks: no packs available" << std::endl;
    return false;
  }

  UResourcePackResolver resolver;
  const auto packs = resolver.Resolve(selection, core.WorkDir, core.ExeDir);
  if (packs.empty())
  {
    std::cerr << "UCore::ApplyResourcePacks: no packs resolved" << std::endl;
    return false;
  }

  if (!core.PlaceholderCacheInstance)
  {
    InitPlaceholderCache(core);
  }

  core.BlockMergeRegistryInstance->SetPrimaryPackIds(selection.Primary);
  core.BlockMergeRegistryInstance->SetWorldgenOwnerPackId(selection.WorldgenOwner);
  core.BlockMergeRegistryInstance->Rebuild(packs, core.PlaceholderCacheInstance,
                                           core.ResourcePacks.PlaceholderTileSize);
  core.ActivePackSelection = selection;
  core.ActiveResourcePacksEnabled = selection.AllIds();

  if (core.ObjectLibraryInstance && core.WorldInstance)
  {
    core.ObjectLibraryInstance->LoadMerged(core.ObjectsPath, packs,
                                           core.WorldInstance->GetBlockRegistry());
    core.WorldInstance->SetObjectLibrary(core.ObjectLibraryInstance.get());
  }

  RebuildBlockTexturesFromMergeRegistry(core);

  std::cout << "Resource packs: applied " << packs.size() << " pack(s)";
  for (const auto &p : packs)
  {
    std::cout << " [" << p.Id << "]";
  }
  std::cout << " (" << core.BlockMergeRegistryInstance->GetCubeDescriptors().size()
            << " block types)" << std::endl;

  if (core.WorldInstance)
  {
    core.WorldInstance->OnBlockRegistryChanged();
    RebuildBlockTexturesFromMergeRegistry(core);
  }
  ReloadCreatureCatalog(core, packs);
  if (core.WorldInstance && core.BlockMergeRegistryInstance)
  {
    core.WorldInstance->SetCatalogFingerprint(
        core.BlockMergeRegistryInstance->ComputeCatalogFingerprint());
  }
  return true;
}

void UResourcePackBootstrap::ReloadCreatureCatalog(
    UCore &core, const std::vector<ResourcePackManifest> &packs)
{
  if (!core.WorldInstance || !core.CreatureTextureStorageInstance)
  {
    return;
  }
  auto defs = core.WorldInstance->GetCreatureDefinitionStorage();
  if (!defs)
  {
    return;
  }
  ApplyCreaturePackOverlays(
      *defs, *core.CreatureTextureStorageInstance,
      core.WorldInstance->GetSkinDefinitionStorage().get(),
      core.WorkDir / "models" / "creatures", core.WorkDir / "models" / "skins",
      packs);
  core.WorldInstance->OnCreatureCatalogChanged();
}

void UResourcePackBootstrap::ApplyResourcePacksAfterWorldDataLoaded(UCore &core)
{
  ResourcePackSelection selection;
  selection.Primary = core.WorldInstance->GetResourcePacksPrimary();
  selection.Secondary = core.WorldInstance->GetResourcePacksSecondary();
  selection.WorldgenOwner = core.WorldInstance->GetWorldgenOwnerPackId();
  if (selection.Primary.empty())
  {
    selection.Primary = core.WorldInstance->GetResourcePacksEnabled();
  }
  if (selection.Primary.empty())
  {
    selection = core.GetDefaultResourcePackSelection();
    core.WorldInstance->SetResourcePackSelection(
        selection.Primary, selection.Secondary, selection.WorldgenOwner);
  }
  const std::string storedFingerprint = core.WorldInstance->GetCatalogFingerprint();
  ApplyResourcePacks(core, selection);
  if (core.BlockMergeRegistryInstance && !storedFingerprint.empty())
  {
    const std::string currentFingerprint =
        core.BlockMergeRegistryInstance->ComputeCatalogFingerprint();
    if (storedFingerprint != currentFingerprint)
    {
      std::cerr << "WARNING: block catalog fingerprint mismatch for world '"
                << core.WorldInstance->GetWorldName()
                << "'. Blocks/textures may not match the saved terrain."
                << std::endl;
    }
  }
}

} // namespace cutum
