#ifndef RESOURCEECONOMY_H
#define RESOURCEECONOMY_H

#include "Creatures/Core/CreatureInventory.h"
#include "Game/WorldGameMode.h"

#include <string>

namespace cutum
{

/// Gameplay action costs. Physical rewards are published to the world loot
/// ledger. Creative mode should be treated as "free" at this layer.
struct ResourceEconomyService
{
  /// True when the active entry is available for placement in current mode.
  static bool CanPlace(WorldGameMode mode, const UCreatureInventory &inv,
                       const InventoryEntryRef &activeEntry);

  /// Consumes one unit from inventory/storage if allowed.
  /// Returns false when placement should be rejected.
  static bool ConsumeOnPlace(WorldGameMode mode, UCreatureInventory &inv,
                             const InventoryEntryRef &activeEntry);

  /// Atomically consumes real physical slots (active stack first). Creative
  /// bypasses costs; missing or insufficient stock is rejected without changes.
  static bool ConsumeUnits(WorldGameMode mode, UCreatureInventory &inv,
                           InventoryEntryKind kind, const std::string &itemId,
                           int units = 1);
};

} // namespace cutum

#endif
