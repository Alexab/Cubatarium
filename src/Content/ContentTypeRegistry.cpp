#include "ContentTypeRegistry.h"
#include "BlockDefinitionStorage.h"
#include "Prefab.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum {

namespace {

constexpr const char* kMiscType = "misc";

} // namespace

void ContentTypeRegistry::EnsureDefaultTypes()
{
    if (!typeById_.count(kMiscType)) {
        types_.push_back({kMiscType, "Misc", 999});
        typeById_[kMiscType] = types_.back();
    }
}

void ContentTypeRegistry::LoadTypes(const std::string& typesJsonPath)
{
    types_.clear();
    typeById_.clear();
    std::ifstream file(typesJsonPath);
    if (!file.is_open()) {
        EnsureDefaultTypes();
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    try {
        const auto j = nlohmann::json::parse(buffer.str());
        if (j.contains("types") && j["types"].is_array()) {
            for (const auto& item : j["types"]) {
                ContentType t;
                t.id = item.value("id", kMiscType);
                t.displayName = item.value("displayName", t.id);
                t.sortOrder = item.value("sortOrder", 0);
                types_.push_back(t);
                typeById_[t.id] = t;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ContentTypeRegistry: " << e.what() << std::endl;
    }
    EnsureDefaultTypes();
    std::sort(types_.begin(), types_.end(),
              [](const ContentType& a, const ContentType& b) { return a.sortOrder < b.sortOrder; });
}

std::vector<std::string> ContentTypeRegistry::GetTypesForTags(
    const std::vector<std::string>& tags) const
{
    if (tags.empty()) {
        return {kMiscType};
    }
    return tags;
}

void ContentTypeRegistry::IndexBlocks(const BlockDefinitionStorage& storage)
{
    blockEntries_.clear();
    EnsureDefaultTypes();
    for (const auto& type : types_) {
        blockEntries_[type.id] = {};
    }
    for (const auto& pair : storage.GetAll()) {
        const BlockDefinition& def = pair.second;
        const auto typeIds = GetTypesForTags(def.types);
        CatalogEntry entry{def.name, def.name};
        for (const auto& typeId : typeIds) {
            if (!blockEntries_.count(typeId)) {
                blockEntries_[typeId] = {};
            }
            blockEntries_[typeId].push_back(entry);
        }
    }
    for (auto& pair : blockEntries_) {
        std::sort(pair.second.begin(), pair.second.end(),
                  [](const CatalogEntry& a, const CatalogEntry& b) { return a.id < b.id; });
    }
}

void ContentTypeRegistry::IndexPrefabs(const PrefabLibrary& prefabs)
{
    objectEntries_.clear();
    EnsureDefaultTypes();
    for (const auto& type : types_) {
        objectEntries_[type.id] = {};
    }
    for (const std::string& name : prefabs.ListNames()) {
        const auto typeIds = std::vector<std::string>{kMiscType};
        CatalogEntry entry{name, name};
        for (const auto& typeId : typeIds) {
            objectEntries_[typeId].push_back(entry);
        }
    }
}

std::vector<std::string> ContentTypeRegistry::GetTypeIds(ContentKind kind) const
{
    std::vector<std::string> ids;
    const auto& map = kind == ContentKind::Block ? blockEntries_ : objectEntries_;
    for (const auto& type : types_) {
        if (map.count(type.id) && !map.at(type.id).empty()) {
            ids.push_back(type.id);
        }
    }
    if (ids.empty()) {
        ids.push_back(kMiscType);
    }
    return ids;
}

std::string ContentTypeRegistry::GetTypeDisplayName(const std::string& typeId) const
{
    const auto it = typeById_.find(typeId);
    if (it != typeById_.end()) {
        return it->second.displayName;
    }
    return typeId;
}

std::vector<CatalogEntry> ContentTypeRegistry::GetEntries(ContentKind kind,
                                                          const std::string& typeId) const
{
    const auto& map = kind == ContentKind::Block ? blockEntries_ : objectEntries_;
    const auto it = map.find(typeId);
    if (it != map.end()) {
        return it->second;
    }
    const auto misc = map.find(kMiscType);
    if (misc != map.end()) {
        return misc->second;
    }
    return {};
}

} // namespace cutum
