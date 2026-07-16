#include "Blocks/BlockDefinitionStorage.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace fs = std::filesystem;
using json = nlohmann::json;

std::shared_ptr<const BlockDefinitionCatalog>
UBlockDefinitionStorage::GetCatalogSnapshot() const
{
  return std::atomic_load_explicit(&Active, std::memory_order_acquire);
}

const BlockDefinition *UBlockDefinitionStorage::GetById(BlockId Id) const
{
  // Keep the RCU snapshot alive for this thread until the next Get* call so
  // returned pointers remain valid for synchronous reads (same pattern as
  // UObjectLibrary::Get).
  thread_local std::shared_ptr<const BlockDefinitionCatalog> keep;
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

const BlockDefinition *
UBlockDefinitionStorage::GetByName(const std::string &Name) const
{
  thread_local std::shared_ptr<const BlockDefinitionCatalog> keep;
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
      std::stringstream buffer;
      buffer << file.rdbuf();
      json d;
      try
      {
        d = json::parse(buffer.str());
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
  std::atomic_store_explicit(&Active, std::shared_ptr<const BlockDefinitionCatalog>(
                                          std::move(catalog)),
                              std::memory_order_release);
}

void UBlockDefinitionStorage::ReplaceAll(
    std::unordered_map<BlockId, BlockDefinition> newById,
    std::unordered_map<std::string, BlockId> newNameToId)
{
  auto catalog = std::make_shared<BlockDefinitionCatalog>();
  catalog->ById = std::move(newById);
  catalog->NameToId = std::move(newNameToId);
  std::atomic_store_explicit(&Active, std::shared_ptr<const BlockDefinitionCatalog>(
                                          std::move(catalog)),
                              std::memory_order_release);
}

} // namespace cutum
