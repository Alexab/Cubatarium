#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Core/Memory/VisualFilesystemSource.h"

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

void MeasureVisualPayload(const CreatureGltfMeshAsset &asset, StlSourceStorage &out) noexcept
{
  out.Buffer(asset.nodes.capacity(), sizeof(GltfNodeCpu));
  for (const auto &node : asset.nodes)
    out.Text(node.name);
  out.Buffer(asset.skin.jointNodes.capacity(), sizeof(int));
  out.Buffer(asset.skin.inverseBindMatrices.capacity(), sizeof(glm::mat4));
  out.Buffer(asset.primitives.capacity(), sizeof(GltfPrimitiveCpu));
  for (const auto &primitive : asset.primitives)
  {
    out.Buffer(primitive.mesh.interleavedPosUv.capacity(), sizeof(float));
    out.Buffer(primitive.mesh.indices.capacity(), sizeof(unsigned));
    out.Buffer(primitive.jointIndices.capacity(), sizeof(uint8_t));
    out.Buffer(primitive.jointWeights.capacity(), sizeof(float));
    out.Text(primitive.textureStem);
  }
  out.Buffer(asset.animations.capacity(), sizeof(GltfAnimationCpu));
  for (const auto &clip : asset.animations)
  {
    out.Text(clip.name);
    out.Buffer(clip.channels.capacity(), sizeof(GltfAnimationChannelCpu));
    for (const auto &channel : clip.channels)
    {
      out.Text(channel.path);
      out.Buffer(channel.keyTimes.capacity(), sizeof(float));
      out.Buffer(channel.keyVec3.capacity(), sizeof(glm::vec3));
      out.Buffer(channel.keyQuat.capacity(), sizeof(glm::quat));
    }
  }
  out.Hash(asset.animationIndexByName);
  for (const auto &animation : asset.animationIndexByName)
    out.Text(animation.first);
}

VisualAssetStorage CreatureGltfCache::InspectOwnedStorage(VisualAssetCensus *census) const
{
  std::lock_guard<std::mutex> lock(mutex);
  return InspectWeakVisualAssets<CreatureGltfMeshAsset>(sizeof(*this), creaturesRoot, entries, failedKeys,
      [census](const auto &owner, StlSourceStorage &payload)
      {
        if (census)
          census->Add(owner, VisualAssetOwner::Cache);
        MeasureVisualPayload(*owner, payload);
      });
}

std::shared_ptr<CreatureGltfMeshAsset>
CreatureGltfCache::LoadFromPath(const std::filesystem::path &path)
{
  return Load({},VisualNativeString(VisualAbsolute(path)));
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
      path=VisualLexicalNormal((sourceDirectory.empty()?std::filesystem::path(creaturesRoot)/speciesId:
                                      std::filesystem::path(sourceDirectory))/modelFile);
      key=VisualGenericString(path);
      generation=revision.load(std::memory_order_relaxed);
      if (failedKeys.count(key)) return nullptr;
      if (const auto it=entries.find(key);it!=entries.end())
      {
        if (auto asset=it->second.lock()) return asset;
        entries.erase(it);
      }
    }
    auto asset=CreatureGltfLoader::LoadFromFile(VisualNativeString(path));
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
