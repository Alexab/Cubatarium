#ifndef ITEM_INSPECTION_SOURCE_H
#define ITEM_INSPECTION_SOURCE_H
#include "Items/ItemRenderAsset.h"
#include <stdexcept>

namespace cutum
{
struct ItemInspectionSource
{
  std::string Id;
  std::filesystem::path Path;
  std::shared_ptr<const ItemRenderAsset> Asset;
};
namespace item_inspection_detail
{
inline thread_local const ItemInspectionSource *Current = nullptr;
}
/// Synchronous render-only override. Nested calls and exceptions restore the
/// previous source; no item definition, inventory entry or world asset changes.
class UItemInspectionScope
{
public:
  explicit UItemInspectionScope(ItemInspectionSource source)
      : Source(std::move(source)), Previous(item_inspection_detail::Current)
  {
    if (Source.Id.empty() || Source.Path.empty() || !Source.Asset)
      throw std::invalid_argument("Incomplete item inspection source");
    item_inspection_detail::Current = &Source;
  }
  ~UItemInspectionScope() { item_inspection_detail::Current = Previous; }
  UItemInspectionScope(const UItemInspectionScope &) = delete;
  UItemInspectionScope &operator=(const UItemInspectionScope &) = delete;

private:
  ItemInspectionSource Source;
  const ItemInspectionSource *Previous;
};
inline const ItemInspectionSource *
FindItemInspectionSource(const std::string &id)
{
  const auto *source = item_inspection_detail::Current;
  return source && source->Id == id ? source : nullptr;
}
inline std::string ItemInspectionCacheKey(const std::string &id)
{
  const auto *source = FindItemInspectionSource(id);
  return source ? id + "|inspection:" + source->Path.generic_string() : id;
}
} // namespace cutum
#endif
