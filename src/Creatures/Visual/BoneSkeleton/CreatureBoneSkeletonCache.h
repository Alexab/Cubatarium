#ifndef CREATURE_BONE_SKELETON_CACHE_H
#define CREATURE_BONE_SKELETON_CACHE_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <cstdint>

namespace cutum
{

class CreatureBoneSkeletonCache
{
public:
  static CreatureBoneSkeletonCache &Instance();

  void SetCreaturesRoot(const std::string &root);
  std::string GetCreaturesRoot() const { std::lock_guard<std::mutex> lock(mutex);return creaturesRoot; }
  uint64_t GetRevision() const { return revision.load(std::memory_order_acquire); }
  void Clear();

  std::shared_ptr<const CreatureBoneSkeletonMeshAsset>
  Load(const std::string &speciesId, const std::string &geometryFile,
       const std::string &geometryId = {}, const std::string &sourceDirectory = {});

private:
  std::string creaturesRoot;
  mutable std::mutex mutex;
  std::atomic<uint64_t> revision{1};
  std::unordered_map<std::string, std::weak_ptr<const CreatureBoneSkeletonMeshAsset>>
      cache;
  /// Keys that failed to load (negative cache, mirrors CreatureGltfCache):
  /// stops per-frame file reads and log spam for missing geometry.
  std::unordered_set<std::string> failedKeys;
};

} // namespace cutum

#endif
