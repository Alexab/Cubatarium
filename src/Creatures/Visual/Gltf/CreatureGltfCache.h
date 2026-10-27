#ifndef CREATUREGLTFCACHE_H
#define CREATUREGLTFCACHE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <memory>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cutum
{

class CreatureGltfCache
{
public:
  static CreatureGltfCache &Instance();

  void SetCreaturesRoot(const std::string &root);
  std::string GetCreaturesRoot() const { return creaturesRoot; }
  void Clear();

/// Absolute-path load for non-species assets (item icons, viewmodels):
  /// same weak-ref cache keyed by the path (UX R2).
  std::shared_ptr<CreatureGltfMeshAsset>
  LoadFromPath(const std::filesystem::path &path);
  std::shared_ptr<CreatureGltfMeshAsset> Load(const std::string &speciesId,
                                              const std::string &modelFile);

private:
  CreatureGltfCache() = default;

  std::string creaturesRoot{"models/creatures"};
  std::mutex mutex;
  std::unordered_map<std::string, std::weak_ptr<CreatureGltfMeshAsset>> entries;
};

} // namespace cutum

#endif
