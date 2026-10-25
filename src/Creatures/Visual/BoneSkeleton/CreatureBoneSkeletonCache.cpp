#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"

#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.h"
#include <filesystem>

namespace cutum
{

CreatureBoneSkeletonCache &CreatureBoneSkeletonCache::Instance()
{
  static CreatureBoneSkeletonCache instance;
  return instance;
}

void CreatureBoneSkeletonCache::SetCreaturesRoot(const std::string &root)
{
  std::lock_guard<std::mutex> lock(mutex);
  creaturesRoot = root;
  // Keyed by absolute path: a new root means old entries can never be hit
  // again — drop them (aligned with CreatureGltfCache, audit V12).
  cache.clear();
}

std::shared_ptr<const CreatureBoneSkeletonMeshAsset>
CreatureBoneSkeletonCache::Load(const std::string &speciesId,
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
    // Evict the dead weak_ptr key so the map cannot grow without bound.
    cache.erase(it);
  }

  const auto geometry =
      CreatureBoneSkeletonLoader::LoadFromFile(geoPath.string(), geometryId);
  if (!geometry)
  {
    return nullptr;
  }
  auto asset = std::make_shared<CreatureBoneSkeletonMeshAsset>(
      BoneSkeletonCubeMeshBuilder::BuildMeshAsset(*geometry));
  cache[cacheKey] = asset;
  return asset;
}

} // namespace cutum
