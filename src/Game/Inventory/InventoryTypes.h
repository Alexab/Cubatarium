#ifndef INVENTORY_TYPES_H
#define INVENTORY_TYPES_H

#include <array>
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

struct InventoryEntryRef
{
  InventoryEntryKind kind{InventoryEntryKind::Block};
  std::string Id;
  int count{0};
  bool empty{true};
  /// 0 = new, 1 = fully worn (tools / durable items).
  float wear{0.f};
  bool broken{false};
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
