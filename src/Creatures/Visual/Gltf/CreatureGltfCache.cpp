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
}

void CreatureGltfCache::Clear()
{
  std::lock_guard<std::mutex> lock(mutex);
  entries.clear();
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::Load(const std::string &speciesId,
                        const std::string &modelFile)
{
  const std::string key = speciesId + "|" + modelFile;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (const auto it = entries.find(key); it != entries.end())
    {
      if (auto asset = it->second.lock())
      {
        return asset;
      }
    }
  }

  const std::filesystem::path path =
      std::filesystem::path(creaturesRoot) / speciesId / modelFile;
  auto asset = CreatureGltfLoader::LoadFromFile(path.string());
  if (!asset)
  {
    std::cerr << "CreatureGltfCache: failed to load " << path.string()
              << std::endl;
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex);
  entries[key] = asset;
  return asset;
}

} // namespace cutum
