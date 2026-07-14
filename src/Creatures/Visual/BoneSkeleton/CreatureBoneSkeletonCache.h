#ifndef CREATURE_BONE_SKELETON_CACHE_H
#define CREATURE_BONE_SKELETON_CACHE_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace cutum
{

class CreatureBoneSkeletonCache
{
public:
  static CreatureBoneSkeletonCache &Instance();

  void SetCreaturesRoot(const std::string &root);
  std::string GetCreaturesRoot() const { return creaturesRoot; }

  std::shared_ptr<const CreatureBoneSkeletonMeshAsset>
  Load(const std::string &speciesId, const std::string &geometryFile,
       const std::string &geometryId = {});

private:
  std::string creaturesRoot;
  std::mutex mutex;
  std::unordered_map<std::string, std::weak_ptr<const CreatureBoneSkeletonMeshAsset>>
      cache;
};

} // namespace cutum

#endif
