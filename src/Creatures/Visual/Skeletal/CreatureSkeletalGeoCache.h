#ifndef CREATURE_SKELETAL_GEO_CACHE_H
#define CREATURE_SKELETAL_GEO_CACHE_H

#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace cutum
{

class CreatureSkeletalGeoCache
{
public:
  static CreatureSkeletalGeoCache &Instance();

  void SetCreaturesRoot(const std::string &root);
  std::string GetCreaturesRoot() const { return creaturesRoot; }

  std::shared_ptr<const CreatureSkeletalMeshAsset>
  Load(const std::string &speciesId, const std::string &geometryFile,
       const std::string &geometryId = {});

private:
  std::string creaturesRoot;
  std::mutex mutex;
  std::unordered_map<std::string, std::weak_ptr<const CreatureSkeletalMeshAsset>>
      cache;
};

} // namespace cutum

#endif
