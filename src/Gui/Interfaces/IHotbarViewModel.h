#ifndef I_HOTBAR_VIEW_MODEL_H
#define I_HOTBAR_VIEW_MODEL_H

#include "InventoryTypes.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace cutum
{

struct HotbarSlotView
{
  std::string id;
  std::string label;
  bool isBlock{true};
  InventoryEntryKind entryKind{InventoryEntryKind::Block};
  bool selected{false};
  int hotkey{-1};
};

class IHotbarViewModel
{
public:
  virtual ~IHotbarViewModel() = default;
  virtual size_t GetBarCount() const = 0;
  virtual std::array<HotbarSlotView, 10> GetBarSlots(size_t barIndex) const = 0;
  virtual size_t GetSelectedSlot(size_t barIndex) const = 0;
  virtual void SelectSlot(size_t barIndex, size_t slotIndex) = 0;
  virtual bool AssignSlot(size_t barIndex, size_t slotIndex,
                          const InventoryEntryRef &entry) = 0;
  virtual void BeginPendingAssignment(const InventoryEntryRef &entry) = 0;
  virtual bool HasPendingAssignment() const = 0;
  virtual bool ApplyPendingAssignment(size_t barIndex, size_t slotIndex) = 0;
  virtual void ClearPendingAssignment() = 0;
};

} // namespace cutum

#endif
