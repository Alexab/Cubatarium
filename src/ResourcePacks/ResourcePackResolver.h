#ifndef RESOURCEPACKRESOLVER_H
#define RESOURCEPACKRESOLVER_H

#include "ResourcePacks/ResourcePack.h"
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cutum
{

struct ResourcePacksConfig
{
  std::vector<std::string> DefaultPrimary;
  std::vector<std::string> DefaultSecondary;
  std::vector<std::string> DefaultEnabled;
  int PlaceholderTileSize{16};
  std::string PlaceholderBackground{"#6b4a9e"};
};

struct ResourcePackSelection
{
  std::vector<std::string> Primary;
  std::vector<std::string> Secondary;
  std::string WorldgenOwner;

  std::vector<std::string> AllIds() const
  {
    std::vector<std::string> all = Primary;
    all.insert(all.end(), Secondary.begin(), Secondary.end());
    return all;
  }
};

struct InstalledPackInfo
{
  std::string Id;
  std::string DisplayName;
  int Priority{0};
  int Resolution{16};
  std::string License;
  std::string MinGameVersion;
  std::vector<std::string> Depends;
  std::vector<std::string> Conflicts;
  WorldgenRole Role{WorldgenRole::Secondary};
  bool FromWritableRoot{false};
};

inline const std::vector<std::string> &DefaultEnabledResourcePacks()
{
  static const std::vector<std::string> kDefaults = {"kenney_voxel_16",
                                                     "cubatarium_cc0_base"};
  return kDefaults;
}

class UResourcePackResolver
{
public:
  static ResourcePacksConfig ParseFromJson(const nlohmann::json &root);

  static std::vector<InstalledPackInfo>
  ListInstalled(const std::filesystem::path &assetRoot,
                const std::filesystem::path &writableRoot);

  std::vector<ResourcePackManifest>
  Resolve(const ResourcePackSelection &selection,
          const std::filesystem::path &assetRoot,
          const std::filesystem::path &writableRoot) const;

  std::vector<ResourcePackManifest>
  Resolve(const std::vector<std::string> &enabledIds,
          const std::filesystem::path &assetRoot,
          const std::filesystem::path &writableRoot) const;
};

} // namespace cutum

#endif
