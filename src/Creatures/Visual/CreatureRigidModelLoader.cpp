#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Core/Json/JsonVec.h"
#include "Core/Memory/VisualAssetStorage.h"
#include "WorldGen/Core/CatalogJsonSource.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace cutum
{

namespace
{

std::shared_ptr<const CreatureRigidModelParts> ReadParts(const std::string &path)
{
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "CreatureRigidModelCache: cannot open " << path << std::endl;
    return {};
  }

  CatalogJsonDocument data;
  try
  {
    data = ParseAndCloseCatalogFileJson(file, CatalogJsonKind::Definitions);
  }
  catch (const nlohmann::json::exception &ex)
  {
    std::cerr << "CreatureRigidModelCache: parse error in " << path << ": "
              << ex.what() << std::endl;
    return {};
  }

  const auto source = data.find("parts");
  if (source == data.end() || !source->is_array())
  {
    std::cerr << "CreatureRigidModelCache: missing parts[] in " << path
              << std::endl;
    return {};
  }
  if (source->empty())
    return {};

  auto parts = std::make_shared<CreatureRigidModelParts>();
  parts->reserve(source->size());
  for (const auto &partJson : *source)
  {
    auto &part = parts->emplace_back();
    part.Id = partJson.value("id", "");
    part.textureStem = partJson.value("texture", "body");
    part.offsetBlocks = ReadVec3(
        partJson.value("offset", nlohmann::json::array()), part.offsetBlocks);
    part.sizeBlocks = ReadVec3(
        partJson.value("size", nlohmann::json::array()), part.sizeBlocks);
    part.HasPivot = partJson.contains("pivot");
    if (part.HasPivot)
    {
      part.PivotBlocks = ReadVec3(
          partJson.value("pivot", nlohmann::json::array()), part.PivotBlocks);
    }
    part.LimbKind = partJson.value("limb", "");
    part.LimbAxis = partJson.value("limb_axis", "x");
  }
  return parts;
}

} // namespace

CreatureRigidModelCache &CreatureRigidModelCache::Instance()
{
  static CreatureRigidModelCache cache;
  return cache;
}

void CreatureRigidModelCache::Clear()
{
  std::unique_ptr<PartTable> retired;
  {
    const std::lock_guard lock(CacheMutex);
    ++Revision;
    retired = std::move(Cache);
  }
}

VisualAssetStorage CreatureRigidModelCache::InspectOwnedStorage() const
{
  const std::lock_guard lock(CacheMutex);
  VisualAssetStorage result;
  result.inlineBytes = sizeof(*this);
  StlSourceStorage table, control, payload;
  if (Cache)
  {
    table.Add(sizeof(PartTable));
    table.Hash(*Cache);
    result.live = Cache->size();
    for (const auto &[key, parts] : *Cache)
    {
      table.Text(key);
      AddVisualAssetControl<CreatureRigidModelParts>(control);
      payload.Buffer(parts->capacity(), sizeof(CreatureVisualPartDef));
      for (const auto &part : *parts)
      {
        payload.Text(part.Id);
        payload.Text(part.textureStem);
        payload.Text(part.LimbKind);
        payload.Text(part.LimbAxis);
      }
    }
  }
  FinishVisualAssetStorage(result, table, control, payload);
  return result;
}

std::shared_ptr<const CreatureRigidModelParts> CreatureRigidModelCache::LoadParts(
    const std::string &speciesDir, const std::string &relativePath)
{
  const std::string path = speciesDir + "/" + relativePath;
  const std::string cacheKey =
      std::filesystem::absolute(path).lexically_normal().generic_string();
  for (;;)
  {
    uint64_t revision;
    {
      const std::lock_guard lock(CacheMutex);
      if (Cache)
      {
        if (const auto it = Cache->find(cacheKey); it != Cache->end())
          return it->second;
      }
      revision = Revision;
    }

    auto parts = ReadParts(path);
    const std::lock_guard lock(CacheMutex);
    if (Revision != revision)
      continue;
    if (!parts)
      return {};
    if (!Cache)
    {
      auto candidate = std::make_unique<PartTable>();
      candidate->try_emplace(cacheKey, parts);
      Cache = std::move(candidate);
      return parts;
    }
    return Cache->try_emplace(cacheKey, std::move(parts)).first->second;
  }
}

} // namespace cutum
