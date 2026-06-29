#include "Creatures/Visual/Skeletal/CreatureSkeletalGeoCache.h"

#include "Creatures/Visual/Skeletal/SkeletalCubeMeshBuilder.h"
#include "Creatures/Visual/Skeletal/CreatureSkeletalGeoLoader.h"
#include <filesystem>

namespace cutum
{

CreatureSkeletalGeoCache &CreatureSkeletalGeoCache::Instance()
{
  static CreatureSkeletalGeoCache instance;
  return instance;
}

void CreatureSkeletalGeoCache::SetCreaturesRoot(const std::string &root)
{
  std::lock_guard<std::mutex> lock(mutex);
  creaturesRoot = root;
}

std::shared_ptr<const CreatureSkeletalMeshAsset>
CreatureSkeletalGeoCache::Load(const std::string &speciesId,
                              const std::string &geometryFile,
                              const std::string &geometryId)
{
  const std::filesystem::path geoPath =
      std::filesystem::path(creaturesRoot) / speciesId / geometryFile;
  const std::string cacheKey = geoPath.string() + "|" + geometryId;
  std::lock_guard<std::mutex> lock(mutex);
  if (const auto it = cache.find(cacheKey); it != cache.end())
  {
    if (auto existing = it->second.lock())
    {
      return existing;
    }
  }

  const auto geometry =
      CreatureSkeletalGeoLoader::LoadFromFile(geoPath.string(), geometryId);
  if (!geometry)
  {
    return nullptr;
  }
  auto asset = std::make_shared<CreatureSkeletalMeshAsset>(
      SkeletalCubeMeshBuilder::BuildMeshAsset(*geometry));
  cache[cacheKey] = asset;
  return asset;
}

} // namespace cutum
