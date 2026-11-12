#ifndef CREATUREGLTFCACHE_H
#define CREATUREGLTFCACHE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <memory>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <cstdint>

namespace cutum
{

class CreatureGltfCache
{
public:
  static CreatureGltfCache &Instance();

  void SetCreaturesRoot(const std::string &root);
  std::string GetCreaturesRoot() const { std::lock_guard<std::mutex> lock(mutex);return creaturesRoot; }
  uint64_t GetRevision() const { return revision.load(std::memory_order_acquire); }
  void Clear();

/// Absolute-path load for non-species assets (item icons, viewmodels):
/// same weak-ref cache keyed by the path (UX R2).
  std::shared_ptr<CreatureGltfMeshAsset>
  LoadFromPath(const std::filesystem::path &path);
  std::shared_ptr<CreatureGltfMeshAsset> Load(const std::string &speciesId,
      const std::string &modelFile, const std::string &sourceDirectory = {});

private:
  CreatureGltfCache() = default;

  std::string creaturesRoot{"models/creatures"};
  mutable std::mutex mutex;
  std::atomic<uint64_t> revision{1};
  std::unordered_map<std::string, std::weak_ptr<CreatureGltfMeshAsset>> entries;
  /// Keys that failed to load: without this every frame of a missing model
  /// re-read the file from disk and re-logged the failure. Cleared together
  /// with the entries (root change / Clear) so a pack reload retries.
  std::unordered_set<std::string> failedKeys;
};

} // namespace cutum

#endif
