#ifndef I_INVENTORY_VIEW_MODEL_H
#define I_INVENTORY_VIEW_MODEL_H

#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <string>
#include <vector>

namespace cutum
{

enum class InventoryMode
{
  Creative,
  Owned
};

struct InventoryGroupView
{
  std::string Id;
  std::string label;
  int order{0};
};

struct InventoryEntryView
{
  InventoryEntryRef ref;
  std::string label;
};

class IInventoryViewModel
{
public:
  virtual ~IInventoryViewModel() = default;
  virtual std::vector<InventoryGroupView>
  GetGroups(ContentKind tab, InventoryMode mode) const = 0;
  virtual std::vector<InventoryEntryView>
  GetEntries(ContentKind tab, const std::string &groupId,
             InventoryMode mode) const = 0;
  virtual bool CanAssignToHotbar(const InventoryEntryRef &entry,
                                 size_t barIndex, size_t slotIndex) const = 0;
  virtual bool AssignToHotbar(const InventoryEntryRef &entry, size_t barIndex,
                              size_t slotIndex) = 0;
  virtual InventoryMode GetInventoryMode() const = 0;
  virtual void SetInventoryMode(InventoryMode mode) = 0;
};

} // namespace cutum

#endif
