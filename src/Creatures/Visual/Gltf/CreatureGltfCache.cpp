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
}

void CreatureGltfCache::Clear()
{
  std::lock_guard<std::mutex> lock(mutex);
  entries.clear();
  failedKeys.clear();
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::LoadFromPath(const std::filesystem::path &path)
{
  const std::string key = path.string();
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (failedKeys.count(key) != 0)
    {
      return nullptr;
    }
    if (const auto it = entries.find(key); it != entries.end())
    {
      if (auto asset = it->second.lock())
      {
        return asset;
      }
      entries.erase(it);
    }
  }
  auto asset = CreatureGltfLoader::LoadFromFile(path.string());
  if (!asset)
  {
    std::lock_guard<std::mutex> lock(mutex);
    failedKeys.insert(key);
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(mutex);
  entries[key] = asset;
  return asset;
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::Load(const std::string &speciesId,
                        const std::string &modelFile)
{
  const std::string key = speciesId + "|" + modelFile;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (failedKeys.count(key) != 0)
    {
      return nullptr;
    }
    if (const auto it = entries.find(key); it != entries.end())
    {
      if (auto asset = it->second.lock())
      {
        return asset;
      }
      // Evict the dead weak_ptr key so the map cannot grow without bound.
      entries.erase(it);
    }
  }

  const std::filesystem::path path =
      std::filesystem::path(creaturesRoot) / speciesId / modelFile;
  auto asset = CreatureGltfLoader::LoadFromFile(path.string());
  if (!asset)
  {
    // Logged once per key: the negative cache above stops the retries that
    // used to spam this per frame.
    std::cerr << "CreatureGltfCache: failed to load " << path.string()
              << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    failedKeys.insert(key);
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex);
  entries[key] = asset;
  return asset;
}

} // namespace cutum
