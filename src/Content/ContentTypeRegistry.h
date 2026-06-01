#ifndef CONTENT_TYPE_REGISTRY_H
#define CONTENT_TYPE_REGISTRY_H

#include "ContentType.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <unordered_map>
#include <vector>

namespace cutum {

class BlockDefinitionStorage;
class PrefabLibrary;

class ContentTypeRegistry : public IContentCatalog {
public:
    void LoadTypes(const std::string& typesJsonPath);
    void IndexBlocks(const BlockDefinitionStorage& storage);
    void IndexPrefabs(const PrefabLibrary& prefabs);

    std::vector<std::string> GetTypeIds(ContentKind kind) const override;
    std::string GetTypeDisplayName(const std::string& typeId) const override;
    std::vector<CatalogEntry> GetEntries(ContentKind kind,
                                         const std::string& typeId) const override;

private:
    void EnsureDefaultTypes();
    std::vector<std::string> GetTypesForTags(const std::vector<std::string>& tags) const;

    std::vector<ContentType> types_;
    std::unordered_map<std::string, ContentType> typeById_;
    std::unordered_map<std::string, std::vector<CatalogEntry>> blockEntries_;
    std::unordered_map<std::string, std::vector<CatalogEntry>> objectEntries_;
};

} // namespace cutum

#endif
