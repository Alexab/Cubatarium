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
  std::vector<std::string> DefaultEnabled;
  int PlaceholderTileSize{16};
  std::string PlaceholderBackground{"#6b4a9e"};
};

struct InstalledPackInfo
{
  std::string Id;
  std::string DisplayName;
  int Priority{0};
  int Resolution{16};
  std::string License;
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
  Resolve(const std::vector<std::string> &enabledIds,
          const std::filesystem::path &assetRoot,
          const std::filesystem::path &writableRoot) const;
};

} // namespace cutum

#endif
