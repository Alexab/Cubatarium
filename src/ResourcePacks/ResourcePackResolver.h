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
  bool UseResourcePacks{true};
  std::vector<std::string> Enabled;
  int PlaceholderTileSize{16};
  std::string PlaceholderBackground{"#6b4a9e"};
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

  std::vector<ResourcePackManifest>
  Resolve(const ResourcePacksConfig &cfg,
          const std::filesystem::path &assetRoot,
          const std::filesystem::path &writableRoot) const;
};

} // namespace cutum

#endif
