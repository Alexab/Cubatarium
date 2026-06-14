#ifndef I_CONTENT_CATALOG_H
#define I_CONTENT_CATALOG_H

#include <string>
#include <vector>

namespace cutum {

enum class ContentKind { Block, UObject, UCreature, Skin };

struct CatalogEntry {
    std::string id;
    std::string displayName;
};

class IContentCatalog {
public:
    virtual ~IContentCatalog() = default;
    virtual std::vector<std::string> GetTypeIds(ContentKind kind) const = 0;
    virtual std::string GetTypeDisplayName(const std::string& typeId) const = 0;
    virtual std::vector<CatalogEntry> GetEntries(ContentKind kind,
                                                 const std::string& typeId) const = 0;
};

} // namespace cutum

#endif
