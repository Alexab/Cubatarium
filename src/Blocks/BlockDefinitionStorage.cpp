#include "WorldGen/Core/CatalogJsonSource.h"
#include "Blocks/BlockDefinitionStorage.h"
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace fs = std::filesystem;
using json = nlohmann::json;

std::shared_ptr<const BlockDefinitionCatalog>
UBlockDefinitionStorage::GetCatalogSnapshot() const
{
  return std::atomic_load_explicit(&Active, std::memory_order_acquire);
}

uint64_t UBlockDefinitionStorage::NextRevision()
{
  static std::atomic<uint64_t> counter{1};
  return counter.fetch_add(1, std::memory_order_relaxed);
}

UBlockDefinitionStorage::ReadCaches &UBlockDefinitionStorage::ThreadReads()
{thread_local ReadCaches caches;return caches;}

std::array<std::shared_ptr<const BlockDefinitionCatalog>,3> UBlockDefinitionStorage::GetThreadReadSnapshots()
{const auto &caches=ThreadReads();return {caches.byId,caches.byName,caches.fast};}

size_t UBlockDefinitionStorage::ThreadReadInlineBytes() {return sizeof(ReadCaches);}

const std::shared_ptr<const BlockDefinitionCatalog> &
UBlockDefinitionStorage::ThreadSnapshot() const
{
  // Keep the RCU snapshot alive for this thread until it is refreshed so
  // pointers returned by Get* stay valid for synchronous reads (same pattern
  // as UObjectLibrary::Get). The catalog is only re-fetched when the storage
  // publishes a new one: GetById sits on per-block hot paths (chunk commit).
  auto &cache=ThreadReads();
  const uint64_t revision = Revision.load(std::memory_order_acquire);
  if (cache.owner != this || cache.revision != revision)
  {
    cache.fast = std::atomic_load_explicit(&Active, std::memory_order_acquire);
    cache.owner = this;
    cache.revision = revision;
  }
  return cache.fast;
}

void UBlockDefinitionStorage::Publish(
    std::shared_ptr<const BlockDefinitionCatalog> catalog)
{
  std::atomic_store_explicit(&Active, std::move(catalog),
                             std::memory_order_release);
  Revision.store(NextRevision(), std::memory_order_release);
}

const BlockDefinition *UBlockDefinitionStorage::GetById(BlockId Id) const
{
  // Keep the RCU snapshot alive for this thread until the next Get* call so
  // returned pointers remain valid for synchronous reads (same pattern as
  // UObjectLibrary::Get).
  auto &keep=ThreadReads().byId;
  keep = GetCatalogSnapshot();
  if (!keep)
  {
    return nullptr;
  }
  const auto it = keep->ById.find(Id);
  if (it != keep->ById.end())
  {
    return &it->second;
  }
  return nullptr;
}

const BlockDefinition *UBlockDefinitionStorage::GetByIdFast(BlockId Id) const
{
  const std::shared_ptr<const BlockDefinitionCatalog> &keep = ThreadSnapshot();
  if (!keep)
  {
    return nullptr;
  }
  const auto it = keep->ById.find(Id);
  return it != keep->ById.end() ? &it->second : nullptr;
}

const BlockDefinition *
UBlockDefinitionStorage::GetByName(const std::string &Name) const
{
  auto &keep=ThreadReads().byName;
  keep = GetCatalogSnapshot();
  if (!keep)
  {
    return nullptr;
  }
  const auto it = keep->NameToId.find(Name);
  if (it == keep->NameToId.end())
  {
    return nullptr;
  }
  const auto byId = keep->ById.find(it->second);
  if (byId == keep->ById.end())
  {
    return nullptr;
  }
  return &byId->second;
}

void UBlockDefinitionStorage::Load(const std::string &modelsPath)
{
  auto catalog = std::make_shared<BlockDefinitionCatalog>();
  try
  {
    for (const auto &entry : fs::directory_iterator(modelsPath))
    {
      if (entry.path().extension() != ".json")
      {
        continue;
      }
      std::ifstream file(entry.path());
      if (!file.is_open())
      {
        continue;
      }
      CatalogJsonDocument d;
      try
      {
        d = ParseAndCloseCatalogFileJson(file,CatalogJsonKind::Definitions);
      }
      catch (const json::exception &e)
      {
        std::cerr << "UBlockDefinitionStorage: parse error " << entry.path()
                  << ": " << e.what() << std::endl;
        continue;
      }
      BlockDefinition def;
      def.Name = d.value("name", "");
      def.Id = static_cast<BlockId>(d.value("id", 0));
      if (def.Name.empty() || def.Id == BLOCK_AIR)
      {
        continue;
      }
      if (d.contains("animation"))
      {
        def.Animation = ParseAnimationFromJson(d["animation"]);
      }
      if (d.contains("physics"))
      {
        def.Physics = ParsePhysicsFromJson(d["physics"]);
      }
      else
      {
        def.Physics = BlockPhysicsProfile::Solid();
      }
      if (d.contains("render"))
      {
        def.Render = ParseRenderFromJson(d["render"]);
      }
      if (d.contains("types") && d["types"].is_array())
      {
        for (const auto &t : d["types"])
        {
          if (t.is_string())
          {
            def.Types.push_back(t.get<std::string>());
          }
        }
      }
      if (d.contains("physics") && d["physics"].is_object() &&
          d["physics"].contains("preset") && d["physics"]["preset"].is_string())
      {
        ApplyRenderPresetDefaults(def.Render,
                                  d["physics"]["preset"].get<std::string>());
      }
      catalog->ById[def.Id] = def;
      catalog->NameToId[def.Name] = def.Id;
    }
  }
  catch (const fs::filesystem_error &ex)
  {
    std::cerr << "UBlockDefinitionStorage::Load: " << ex.what() << std::endl;
  }
  Publish(std::shared_ptr<const BlockDefinitionCatalog>(std::move(catalog)));
}

void UBlockDefinitionStorage::ReplaceAll(
    std::unordered_map<BlockId, BlockDefinition> newById,
    std::unordered_map<std::string, BlockId> newNameToId)
{
  auto catalog = std::make_shared<BlockDefinitionCatalog>();
  catalog->ById = std::move(newById);
  catalog->NameToId = std::move(newNameToId);
  Publish(std::shared_ptr<const BlockDefinitionCatalog>(std::move(catalog)));
}

} // namespace cutum
