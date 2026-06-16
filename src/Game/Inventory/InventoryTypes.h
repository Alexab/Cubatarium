#ifndef INVENTORY_TYPES_H
#define INVENTORY_TYPES_H

#include <array>
#include <string>

namespace cutum
{

enum class InventoryEntryKind
{
  Block,
  UObject,
  UCreature,
  Skin
};

struct InventoryEntryRef
{
  InventoryEntryKind kind{InventoryEntryKind::Block};
  std::string Id;
  int count{0};
  bool empty{true};
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
