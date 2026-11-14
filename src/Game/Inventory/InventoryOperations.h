#pragma once
#include "Creatures/Core/CreatureInventory.h"
#include <functional>
#include <utility>
namespace cutum
{
class UInventoryOperations
{
public:
  static InventoryOperationResult
  TransferToWorld(UCreatureInventory &inventory, size_t slot, int units,
                  const std::function<bool(const InventoryEntryRef &)> &publish)
  {
    auto trial = inventory;
    InventoryEntryRef payload;
    auto result = trial.Take(slot, units, payload);
    if (!result)
      return result;
    if (!publish(payload))
      return {0, units, "world_unavailable"};
    inventory = std::move(trial);
    return result;
  }
  static InventoryOperationResult TransferEquipmentToWorld(
      UCreatureInventory &inventory, bool offhand, size_t slot,
      const std::function<bool(const InventoryEntryRef &)> &publish)
  {
    auto trial = inventory;
    InventoryEntryRef payload;
    auto result = trial.TakeEquipment(offhand, slot, payload);
    if (!result)
      return result;
    if (!publish(payload))
      return {0, 1, "world_unavailable"};
    inventory = std::move(trial);
    return result;
  }
  static bool Matches(const UCreatureInventory &inventory, uint64_t revision,
                      bool creative)
  {
    return inventory.IsCreative() == creative &&
           inventory.GetRevision() == revision;
  }
};
} // namespace cutum
