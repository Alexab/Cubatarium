#include "Creatures/Visual/CreatureRigidModelLoader.h"
#include "Core/Json/JsonVec.h"
#include "Core/Memory/VisualAssetCensus.h"
#include "Core/Memory/VisualFilesystemSource.h"
#include "Core/Memory/VisualLoaderSource.h"

#include <filesystem>
#include <iostream>

namespace cutum
{

namespace
{

std::shared_ptr<const CreatureRigidModelParts> ReadParts(const std::string &path)
{
  VisualLoaderSource load(VisualLoaderKind::Rigid);
  const std::filesystem::path modelPath(path);
  std::optional<CatalogJsonDocument> document;
  try
  {
    document = load.ReadDocument(modelPath, CatalogJsonKind::Definitions);
  }
  catch (const nlohmann::json::exception &ex)
  {
    std::cerr << "CreatureRigidModelCache: parse error in " << path << ": "
              << ex.what() << std::endl;
    return {};
  }
  if (!document)
  {
    std::cerr << "CreatureRigidModelCache: cannot open " << path << std::endl;
    return {};
  }

  const auto &data = *document;
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
  load.Success();
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

void MeasureVisualPayload(const CreatureRigidModelParts &parts, StlSourceStorage &out) noexcept
{
  out.Buffer(parts.capacity(), sizeof(CreatureVisualPartDef));
  for (const auto &part : parts)
  {
    out.Text(part.Id);
    out.Text(part.textureStem);
    out.Text(part.LimbKind);
    out.Text(part.LimbAxis);
  }
}

VisualAssetStorage CreatureRigidModelCache::InspectOwnedStorage(VisualAssetCensus *census) const
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
      if (census)
        census->Add(parts, VisualAssetOwner::Cache);
      AddVisualAssetControl<CreatureRigidModelParts>(control);
      MeasureVisualPayload(*parts, payload);
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
      VisualGenericString(VisualLexicalNormal(VisualAbsolute(path)));
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
