#include "Creatures/Visual/Gltf/CreatureGltfCache.h"

#include "Creatures/Visual/Gltf/CreatureGltfLoader.h"
#include <filesystem>
#include <iostream>

namespace cutum
{

CreatureGltfCache &CreatureGltfCache::Instance()
{
  static CreatureGltfCache cache;
  return cache;
}

void CreatureGltfCache::SetCreaturesRoot(const std::string &root)
{
  std::lock_guard<std::mutex> lock(mutex);
  creaturesRoot = root;
  entries.clear();
  failedKeys.clear();
  revision.fetch_add(1,std::memory_order_release);
}

void CreatureGltfCache::Clear()
{
  std::lock_guard<std::mutex> lock(mutex);
  entries.clear();
  failedKeys.clear();
  revision.fetch_add(1,std::memory_order_release);
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::LoadFromPath(const std::filesystem::path &path)
{
  return Load({},std::filesystem::absolute(path).string());
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::Load(const std::string &speciesId,
                        const std::string &modelFile,
                        const std::string &sourceDirectory)
{
  for (;;)
  {
    std::filesystem::path path;
    std::string key;
    uint64_t generation=0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      path=((sourceDirectory.empty()?std::filesystem::path(creaturesRoot)/speciesId:
                                      std::filesystem::path(sourceDirectory))/modelFile).lexically_normal();
      key=path.generic_string();
      generation=revision.load(std::memory_order_relaxed);
      if (failedKeys.count(key)) return nullptr;
      if (const auto it=entries.find(key);it!=entries.end())
      {
        if (auto asset=it->second.lock()) return asset;
        entries.erase(it);
      }
    }
    auto asset=CreatureGltfLoader::LoadFromFile(path.string());
    std::lock_guard<std::mutex> lock(mutex);
    // Clear/root replacement while I/O ran must not republish old content.
    if (generation!=revision.load(std::memory_order_relaxed)) continue;
    if (!asset)
    {
      failedKeys.insert(key);
      std::cerr<<"CreatureGltfCache: failed to load "<<path.string()<<std::endl;
      return nullptr;
    }
    if (const auto it=entries.find(key);it!=entries.end())
      if (auto shared=it->second.lock()) return shared;
    entries[key]=asset;
    return asset;
  }
}

} // namespace cutum
