#include "ResourcePacks/ResourcePackResolver.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <map>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace fs = std::filesystem;

namespace
{

void ParseEnabledArray(const nlohmann::json &arr,
                       std::vector<std::string> &out)
{
  if (!arr.is_array())
  {
    return;
  }
  for (const auto &id : arr)
  {
    if (id.is_string())
    {
      out.push_back(id.get<std::string>());
    }
  }
}

void ScanRootForPacks(const fs::path &root, bool writable,
                      std::map<std::string, InstalledPackInfo> &out)
{
  const fs::path packsDir = root / "resource_packs";
  if (!fs::exists(packsDir) || !fs::is_directory(packsDir))
  {
    return;
  }
  for (const auto &entry : fs::directory_iterator(packsDir))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    const std::string dirName = entry.path().filename().string();
    if (!dirName.empty() && dirName[0] == '_')
    {
      continue;
    }
    if (auto manifest = UResourcePack::LoadManifest(entry.path()))
    {
      InstalledPackInfo info;
      info.Id = manifest->Id;
      info.DisplayName = manifest->Name;
      info.Priority = manifest->Priority;
      info.Resolution = manifest->Resolution;
      info.License = manifest->License;
      info.MinGameVersion = manifest->MinGameVersion;
      info.Depends = manifest->Depends;
      info.Conflicts = manifest->Conflicts;
      info.Role = manifest->Role;
      info.FromWritableRoot = writable;
      const auto it = out.find(info.Id);
      if (it == out.end() || writable)
      {
        out[info.Id] = std::move(info);
      }
    }
  }
}

std::optional<ResourcePackManifest>
LoadPackManifest(const std::string &packId, const fs::path &assetRoot,
                 const fs::path &writableRoot)
{
  const fs::path roots[] = {writableRoot / "resource_packs",
                            assetRoot / "resource_packs"};
  for (const auto &root : roots)
  {
    const auto candidate = root / packId;
    if (auto manifest = UResourcePack::LoadManifest(candidate))
    {
      return manifest;
    }
  }
  return std::nullopt;
}

int ComputeEffectivePriority(int tier, int invertedSelectionIndex, int packPriority)
{
  return tier * 1000 + invertedSelectionIndex * 10 + packPriority;
}

} // namespace

ResourcePacksConfig
UResourcePackResolver::ParseFromJson(const nlohmann::json &root)
{
  ResourcePacksConfig cfg;
  if (root.contains("resource_packs") && root["resource_packs"].is_object())
  {
    const auto &rp = root["resource_packs"];
    if (rp.contains("default_primary") && rp["default_primary"].is_array())
    {
      ParseEnabledArray(rp["default_primary"], cfg.DefaultPrimary);
    }
    if (rp.contains("default_secondary") && rp["default_secondary"].is_array())
    {
      ParseEnabledArray(rp["default_secondary"], cfg.DefaultSecondary);
    }
    if (rp.contains("default_enabled") && rp["default_enabled"].is_array())
    {
      ParseEnabledArray(rp["default_enabled"], cfg.DefaultEnabled);
    }
    else if (rp.contains("enabled") && rp["enabled"].is_array())
    {
      ParseEnabledArray(rp["enabled"], cfg.DefaultEnabled);
    }
    if (cfg.DefaultPrimary.empty() && !cfg.DefaultEnabled.empty())
    {
      cfg.DefaultPrimary = cfg.DefaultEnabled;
    }
    if (rp.contains("placeholder") && rp["placeholder"].is_object())
    {
      const auto &ph = rp["placeholder"];
      cfg.PlaceholderTileSize = ph.value("tile_size", 16);
      cfg.PlaceholderBackground = ph.value("background", "#6b4a9e");
    }
  }
  if (cfg.DefaultPrimary.empty() && cfg.DefaultEnabled.empty())
  {
    cfg.DefaultPrimary = DefaultEnabledResourcePacks();
  }
  return cfg;
}

std::vector<InstalledPackInfo> UResourcePackResolver::ListInstalled(
    const fs::path &assetRoot, const fs::path &writableRoot)
{
  std::map<std::string, InstalledPackInfo> merged;
  ScanRootForPacks(assetRoot, false, merged);
  ScanRootForPacks(writableRoot, true, merged);
  std::vector<InstalledPackInfo> result;
  result.reserve(merged.size());
  for (auto &pair : merged)
  {
    result.push_back(std::move(pair.second));
  }
  std::sort(result.begin(), result.end(),
            [](const InstalledPackInfo &a, const InstalledPackInfo &b) {
              return a.DisplayName < b.DisplayName;
            });
  return result;
}

std::vector<ResourcePackManifest> UResourcePackResolver::Resolve(
    const ResourcePackSelection &selection, const fs::path &assetRoot,
    const fs::path &writableRoot) const
{
  std::vector<ResourcePackManifest> result;
  auto addTier = [&](const std::vector<std::string> &ids, int tier) {
    const int count = static_cast<int>(ids.size());
    for (int i = 0; i < count; ++i)
    {
      const auto &packId = ids[static_cast<size_t>(i)];
      if (auto manifest = LoadPackManifest(packId, assetRoot, writableRoot))
      {
        manifest->SelectionIndex = i;
        manifest->EffectivePriority =
            ComputeEffectivePriority(tier, count - 1 - i, manifest->Priority);
        result.push_back(*manifest);
      }
      else
      {
        std::cerr << "UResourcePackResolver: pack not found: " << packId
                  << std::endl;
      }
    }
  };
  addTier(selection.Primary, 0);
  addTier(selection.Secondary, 1);

  const auto enabled = selection.AllIds();
  std::unordered_set<std::string> enabledSet(enabled.begin(), enabled.end());
  for (const auto &manifest : result)
  {
    for (const std::string &dep : manifest.Depends)
    {
      if (!enabledSet.count(dep))
      {
        std::cerr << "UResourcePackResolver: pack '" << manifest.Id
                  << "' depends on '" << dep << "' which is not enabled"
                  << std::endl;
      }
    }
    for (const std::string &conflict : manifest.Conflicts)
    {
      if (enabledSet.count(conflict))
      {
        std::cerr << "UResourcePackResolver: pack '" << manifest.Id
                  << "' conflicts with enabled pack '" << conflict << "'"
                  << std::endl;
      }
    }
  }

  std::sort(result.begin(), result.end(),
            [](const ResourcePackManifest &a, const ResourcePackManifest &b) {
              return a.EffectivePriority < b.EffectivePriority;
            });
  return result;
}

std::vector<ResourcePackManifest> UResourcePackResolver::Resolve(
    const std::vector<std::string> &enabledIds, const fs::path &assetRoot,
    const fs::path &writableRoot) const
{
  ResourcePackSelection selection;
  selection.Primary = enabledIds;
  return Resolve(selection, assetRoot, writableRoot);
}

} // namespace cutum
