#include "Content/ContentTypeRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Items/ItemDefinitionStorage.h"
#include "World/Objects/ObjectLibrary.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

namespace cutum
{

namespace
{

constexpr const char *kMiscType = "misc";
constexpr const char *kLightingType = "lighting";

void ParseTypeArray(const nlohmann::json &arr,
                    std::vector<ContentType> &out,
                    std::unordered_map<std::string, ContentType> &typeById)
{
  if (!arr.is_array())
  {
    return;
  }
  for (const auto &item : arr)
  {
    ContentType t;
    t.Id = item.value("id", kMiscType);
    t.displayName = item.value("displayName", t.Id);
    t.sortOrder = item.value("sortOrder", 0);
    out.push_back(t);
    typeById[t.Id] = t;
  }
}

} // namespace

void UContentTypeRegistry::EnsureDefaultTypes()
{
  if (!BlockTypeById.count(kMiscType))
  {
    BlockTypes.push_back({kMiscType, "Misc", 999});
    BlockTypeById[kMiscType] = BlockTypes.back();
  }
  if (!ObjectTypeById.count(kMiscType))
  {
    ObjectTypes.push_back({kMiscType, "Misc", 999});
    ObjectTypeById[kMiscType] = ObjectTypes.back();
  }
  if (!CreatureTypeById.count(kMiscType))
  {
    CreatureTypes.push_back({kMiscType, "Misc", 999});
    CreatureTypeById[kMiscType] = CreatureTypes.back();
  }
  if (!ItemTypeById.count(kMiscType))
  {
    ItemTypes.push_back({kMiscType, "Misc", 999});
    ItemTypeById[kMiscType] = ItemTypes.back();
  }
}

void UContentTypeRegistry::LoadTypes(const std::string &typesJsonPath)
{
  BlockTypes.clear();
  ObjectTypes.clear();
  CreatureTypes.clear();
  ItemTypes.clear();
  BlockTypeById.clear();
  ObjectTypeById.clear();
  CreatureTypeById.clear();
  ItemTypeById.clear();
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
    ParseTypeArray(j.value("blockTypes", nlohmann::json::array()), BlockTypes,
                   BlockTypeById);
    ParseTypeArray(j.value("objectTypes", nlohmann::json::array()), ObjectTypes,
                   ObjectTypeById);
    ParseTypeArray(j.value("creatureTypes", nlohmann::json::array()),
                   CreatureTypes, CreatureTypeById);
    ParseTypeArray(j.value("itemTypes", nlohmann::json::array()), ItemTypes,
                   ItemTypeById);
  }
  catch (const std::exception &e)
  {
    std::cerr << "UContentTypeRegistry: " << e.what() << std::endl;
  }
  EnsureDefaultTypes();
  auto sortTypes = [](std::vector<ContentType> &types)
  {
    std::sort(types.begin(), types.end(),
              [](const ContentType &a, const ContentType &b)
              { return a.sortOrder < b.sortOrder; });
  };
  sortTypes(BlockTypes);
  sortTypes(ObjectTypes);
  sortTypes(CreatureTypes);
  sortTypes(ItemTypes);
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
  for (const auto &type : BlockTypes)
  {
    BlockEntries[type.Id] = {};
  }
  for (const auto &pair : storage.GetCatalogSnapshot()->ById)
  {
    const BlockDefinition &def = pair.second;
    const auto typeIds = GetTypesForTags(def.Types);
    const std::string label =
        def.DisplayName.empty() ? HumanizeBlockName(def.Name) : def.DisplayName;
    CatalogEntry entry{def.Name, label};
    for (const auto &typeId : typeIds)
    {
      if (!BlockEntries.count(typeId))
      {
        BlockEntries[typeId] = {};
      }
      BlockEntries[typeId].push_back(entry);
    }
    if (def.Lighting.Emission > 0)
    {
      if (!BlockEntries.count(kLightingType))
      {
        BlockEntries[kLightingType] = {};
      }
      BlockEntries[kLightingType].push_back(entry);
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
  for (const auto &type : CreatureTypes)
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
  for (const auto &type : CreatureTypes)
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

void UContentTypeRegistry::IndexObjects(const UObjectLibrary &objects)
{
  ObjectEntries.clear();
  EnsureDefaultTypes();
  for (const auto &type : ObjectTypes)
  {
    ObjectEntries[type.Id] = {};
  }
  for (const std::string &Name : objects.ListNames())
  {
    const auto typeIds = GetTypesForTags(objects.GetTags(Name));
    const std::string label = objects.GetDisplayName(Name);
    CatalogEntry entry{Name, label};
    for (const auto &typeId : typeIds)
    {
      if (!ObjectEntries.count(typeId))
      {
        ObjectEntries[typeId] = {};
      }
      ObjectEntries[typeId].push_back(entry);
    }
  }
  for (auto &pair : ObjectEntries)
  {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const CatalogEntry &a, const CatalogEntry &b)
              { return a.Id < b.Id; });
  }
}

void UContentTypeRegistry::IndexItems(const UItemDefinitionStorage &storage)
{
  ItemEntries.clear();
  EnsureDefaultTypes();
  std::unordered_set<std::string> knownItemTypes;
  for (const auto &type : ItemTypes)
  {
    ItemEntries[type.Id] = {};
    knownItemTypes.insert(type.Id);
  }
  for (const std::string &Id : storage.ListCatalogIds())
  {
    const auto rawTypeIds = GetTypesForTags(storage.GetTypes(Id));
    // Keep only catalog itemTypes; if none match, fall back to misc so orphans
    // still appear under Tools → Misc.
    std::vector<std::string> typeIds;
    std::unordered_set<std::string> seen;
    for (const std::string &typeId : rawTypeIds)
    {
      if (!knownItemTypes.count(typeId))
      {
        continue;
      }
      if (seen.insert(typeId).second)
      {
        typeIds.push_back(typeId);
      }
    }
    if (typeIds.empty())
    {
      typeIds.push_back(kMiscType);
    }
    CatalogEntry entry{Id, storage.GetDisplayName(Id)};
    for (const auto &typeId : typeIds)
    {
      if (!ItemEntries.count(typeId))
      {
        ItemEntries[typeId] = {};
      }
      ItemEntries[typeId].push_back(entry);
    }
  }
  for (auto &pair : ItemEntries)
  {
    std::sort(pair.second.begin(), pair.second.end(),
              [](const CatalogEntry &a, const CatalogEntry &b)
              { return a.Id < b.Id; });
  }
}

std::vector<std::string>
UContentTypeRegistry::GetTypeIds(ContentKind kind) const
{
  std::vector<std::string> ids;
  const std::unordered_map<std::string, std::vector<CatalogEntry>> *map =
      &BlockEntries;
  const std::vector<ContentType> *types = &BlockTypes;
  if (kind == ContentKind::Object)
  {
    map = &ObjectEntries;
    types = &ObjectTypes;
  }
  else if (kind == ContentKind::UCreature)
  {
    map = &CreatureEntries;
    types = &CreatureTypes;
  }
  else if (kind == ContentKind::Skin)
  {
    map = &SkinEntries;
    types = &CreatureTypes;
  }
  else if (kind == ContentKind::Item)
  {
    map = &ItemEntries;
    types = &ItemTypes;
  }
  for (const auto &type : *types)
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
  if (const auto it = BlockTypeById.find(typeId); it != BlockTypeById.end())
  {
    return it->second.displayName;
  }
  if (const auto it = ObjectTypeById.find(typeId); it != ObjectTypeById.end())
  {
    return it->second.displayName;
  }
  if (const auto it = CreatureTypeById.find(typeId);
      it != CreatureTypeById.end())
  {
    return it->second.displayName;
  }
  if (const auto it = ItemTypeById.find(typeId); it != ItemTypeById.end())
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
  if (kind == ContentKind::Object)
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
  else if (kind == ContentKind::Item)
  {
    map = &ItemEntries;
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
