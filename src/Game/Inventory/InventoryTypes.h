#ifndef INVENTORY_TYPES_H
#define INVENTORY_TYPES_H

#include <array>
#include <cstdint>
#include <string>

namespace cutum
{

enum class InventoryEntryKind
{
  Block,
  Object,
  UCreature,
  Skin,
  Item
};

enum class InventoryEntryForm
{
  FiniteStack,
  CatalogReference
};

struct InventoryEntryRef
{
  bool IsCatalogReference() const
  {
    return form == InventoryEntryForm::CatalogReference || count < 0;
  }
  InventoryEntryKind kind{InventoryEntryKind::Block};
  std::string Id;
  int count{0};
  bool empty{true};
  /// 0 = new, 1 = fully worn (tools / durable items).
  float wear{0.f};
  bool broken{false};
  /// Stable within its owning inventory; moves preserve this identity.
  std::uint64_t instanceId{0};
  std::string contentsId;
  int contentsUnits{0};
  bool pinned{false};
  InventoryEntryForm form{InventoryEntryForm::FiniteStack};
};

struct InventoryOperationResult
{
  int transferred{0};
  int remainder{0};
  std::string reason;
  explicit operator bool() const { return reason.empty() && remainder == 0; }
};

struct HotbarSlot
{
  bool empty{true};
  InventoryEntryRef entry{};
};

struct HotbarBar
{
  std::array<HotbarSlot, 10> slots{};
};

} // namespace cutum

#endif
