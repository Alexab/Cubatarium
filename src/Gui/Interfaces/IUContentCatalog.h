#ifndef IU_CONTENT_CATALOG_H
#define IU_CONTENT_CATALOG_H

#include <string>
#include <vector>

namespace cutum
{

enum class ContentKind
{
  Block,
  Object,
  UCreature,
  Skin,
  Item
};

struct CatalogEntry
{
  std::string Id;
  std::string displayName;
};

class IUContentCatalog
{
public:
  virtual ~IUContentCatalog() = default;
  virtual std::vector<std::string> GetTypeIds(ContentKind kind) const = 0;
  virtual std::string GetTypeDisplayName(const std::string &typeId) const = 0;
  virtual std::vector<CatalogEntry>
  GetEntries(ContentKind kind, const std::string &typeId) const = 0;
};

} // namespace cutum

#endif
