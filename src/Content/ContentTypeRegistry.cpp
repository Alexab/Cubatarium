#include "Content/ContentTypeRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "World/Prefabs/Prefab.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace
{

constexpr const char *kMiscType = "misc";

} // namespace

void UContentTypeRegistry::EnsureDefaultTypes()
{
  if (!TypeById.count(kMiscType))
  {
    Types.push_back({kMiscType, "Misc", 999});
    TypeById[kMiscType] = Types.back();
  }
}

void UContentTypeRegistry::LoadTypes(const std::string &typesJsonPath)
{
  Types.clear();
  TypeById.clear();
  std::ifstream file(typesJsonPath);
  if (!file.is_open())
  {
    EnsureDefaultTypes();
    return;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  try
  {
    const auto j = nlohmann::json::parse(buffer.str());
    if (j.contains("types") && j["types"].is_array())
    {
      for (const auto &item : j["types"])
      {
        ContentType t;
        t.Id = item.value("id", kMiscType);
        t.displayName = item.value("displayName", t.Id);
        t.sortOrder = item.value("sortOrder", 0);
        Types.push_back(t);
        TypeById[t.Id] = t;
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "UContentTypeRegistry: " << e.what() << std::endl;
  }
  EnsureDefaultTypes();
  std::sort(Types.begin(), Types.end(),
            [](const ContentType &a, const ContentType &b)
            { return a.sortOrder < b.sortOrder; });
}

std::vector<std::string> UContentTypeRegistry::GetTypesForTags(
    const std::vector<std::string> &tags) const
{
  if (tags.empty())
  {
    return {kMiscType};
  }
  return tags;
}

void UContentTypeRegistry::IndexBlocks(const UBlockDefinitionStorage &storage)
{
  BlockEntries.clear();
  EnsureDefaultTypes();
  for (const auto &type : Types)
  {
    BlockEntries[type.Id] = {};
  }
  for (const auto &pair : storage.GetAll())
  {
    const BlockDefinition &def = pair.second;
    const auto typeIds = GetTypesForTags(def.Types);
    CatalogEntry entry{def.Name, def.Name};
    for (const auto &typeId : typeIds)
    {
      if (!BlockEntries.count(typeId))
      {
        BlockEntries[typeId] = {};
      }
      BlockEntries[typeId].push_back(entry);
    }
  }
  for (auto &pair : BlockEntries)
  {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const CatalogEntry &a, const CatalogEntry &b)
              { return a.Id < b.Id; });
  }
}

void UContentTypeRegistry::IndexCreatures(
    const UCreatureDefinitionStorage &storage)
{
  CreatureEntries.clear();
  EnsureDefaultTypes();
  for (const auto &type : Types)
  {
    CreatureEntries[type.Id] = {};
  }
  for (const std::string &Id : storage.ListSpawnable())
  {
    const CreatureDefinition *def = storage.Get(Id);
    if (!def)
    {
      continue;
    }
    const auto typeIds = GetTypesForTags(def->catalog.tags);
    const CatalogEntry entry{Id,
                             def->displayName.empty() ? Id : def->displayName};
    for (const auto &typeId : typeIds)
    {
      if (!CreatureEntries.count(typeId))
      {
        CreatureEntries[typeId] = {};
      }
      CreatureEntries[typeId].push_back(entry);
    }
  }
  for (auto &pair : CreatureEntries)
  {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const CatalogEntry &a, const CatalogEntry &b)
              { return a.Id < b.Id; });
  }
}

void UContentTypeRegistry::IndexSkins(const USkinDefinitionStorage &storage)
{
  SkinEntries.clear();
  EnsureDefaultTypes();
  for (const auto &type : Types)
  {
    SkinEntries[type.Id] = {};
  }
  for (const std::string &Id : storage.ListEquippable())
  {
    const SkinDefinition *def = storage.Get(Id);
    if (!def)
    {
      continue;
    }
    const auto typeIds = GetTypesForTags(def->catalog.tags);
    const CatalogEntry entry{Id,
                             def->displayName.empty() ? Id : def->displayName};
    for (const auto &typeId : typeIds)
    {
      if (!SkinEntries.count(typeId))
      {
        SkinEntries[typeId] = {};
      }
      SkinEntries[typeId].push_back(entry);
    }
  }
  for (auto &pair : SkinEntries)
  {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const CatalogEntry &a, const CatalogEntry &b)
              { return a.Id < b.Id; });
  }
}

void UContentTypeRegistry::IndexPrefabs(const UPrefabLibrary &prefabs)
{
  ObjectEntries.clear();
  EnsureDefaultTypes();
  for (const auto &type : Types)
  {
    ObjectEntries[type.Id] = {};
  }
  for (const std::string &Name : prefabs.ListNames())
  {
    const auto typeIds = std::vector<std::string>{kMiscType};
    CatalogEntry entry{Name, Name};
    for (const auto &typeId : typeIds)
    {
      ObjectEntries[typeId].push_back(entry);
    }
  }
}

std::vector<std::string>
UContentTypeRegistry::GetTypeIds(ContentKind kind) const
{
  std::vector<std::string> ids;
  const std::unordered_map<std::string, std::vector<CatalogEntry>> *map =
      &BlockEntries;
  if (kind == ContentKind::UObject)
  {
    map = &ObjectEntries;
  }
  else if (kind == ContentKind::UCreature)
  {
    map = &CreatureEntries;
  }
  else if (kind == ContentKind::Skin)
  {
    map = &SkinEntries;
  }
  for (const auto &type : Types)
  {
    if (map->count(type.Id) && !map->at(type.Id).empty())
    {
      ids.push_back(type.Id);
    }
  }
  if (ids.empty())
  {
    ids.push_back(kMiscType);
  }
  return ids;
}

std::string
UContentTypeRegistry::GetTypeDisplayName(const std::string &typeId) const
{
  const auto it = TypeById.find(typeId);
  if (it != TypeById.end())
  {
    return it->second.displayName;
  }
  return typeId;
}

std::vector<CatalogEntry>
UContentTypeRegistry::GetEntries(ContentKind kind,
                                 const std::string &typeId) const
{
  const std::unordered_map<std::string, std::vector<CatalogEntry>> *map =
      &BlockEntries;
  if (kind == ContentKind::UObject)
  {
    map = &ObjectEntries;
  }
  else if (kind == ContentKind::UCreature)
  {
    map = &CreatureEntries;
  }
  else if (kind == ContentKind::Skin)
  {
    map = &SkinEntries;
  }
  const auto it = map->find(typeId);
  if (it != map->end())
  {
    return it->second;
  }
  const auto misc = map->find(kMiscType);
  if (misc != map->end())
  {
    return misc->second;
  }
  return {};
}

} // namespace cutum
