#include "ResourcePacks/ResourcePackResolver.h"
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

ResourcePacksConfig
UResourcePackResolver::ParseFromJson(const nlohmann::json &root)
{
  ResourcePacksConfig cfg;
  cfg.UseResourcePacks = root.value("use_resource_packs", true);
  if (root.contains("resource_packs") && root["resource_packs"].is_object())
  {
    const auto &rp = root["resource_packs"];
    if (rp.contains("enabled") && rp["enabled"].is_array())
    {
      for (const auto &id : rp["enabled"])
      {
        if (id.is_string())
        {
          cfg.Enabled.push_back(id.get<std::string>());
        }
      }
    }
    if (rp.contains("placeholder") && rp["placeholder"].is_object())
    {
      const auto &ph = rp["placeholder"];
      cfg.PlaceholderTileSize = ph.value("tile_size", 16);
      cfg.PlaceholderBackground = ph.value("background", "#6b4a9e");
    }
  }
  if (cfg.Enabled.empty())
  {
    cfg.Enabled = DefaultEnabledResourcePacks();
    std::cout << "Resource packs: enabled list empty — using defaults: "
              << cfg.Enabled[0];
    for (size_t i = 1; i < cfg.Enabled.size(); ++i)
    {
      std::cout << ", " << cfg.Enabled[i];
    }
    std::cout << std::endl;
  }
  return cfg;
}

std::vector<ResourcePackManifest> UResourcePackResolver::Resolve(
    const ResourcePacksConfig &cfg, const std::filesystem::path &assetRoot,
    const std::filesystem::path &writableRoot) const
{
  std::vector<ResourcePackManifest> result;
  const std::filesystem::path roots[] = {writableRoot / "resource_packs",
                                         assetRoot / "resource_packs"};
  for (const auto &packId : cfg.Enabled)
  {
    bool found = false;
    for (const auto &root : roots)
    {
      const auto candidate = root / packId;
      if (auto manifest = UResourcePack::LoadManifest(candidate))
      {
        result.push_back(*manifest);
        found = true;
        break;
      }
    }
    if (!found)
    {
      std::cerr << "UResourcePackResolver: pack not found: " << packId
                << std::endl;
    }
  }
  std::sort(result.begin(), result.end(),
            [](const ResourcePackManifest &a, const ResourcePackManifest &b) {
              return a.Priority < b.Priority;
            });
  return result;
}

} // namespace cutum
