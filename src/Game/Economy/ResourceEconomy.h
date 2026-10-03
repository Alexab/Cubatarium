#ifndef RESOURCEECONOMY_H
#define RESOURCEECONOMY_H

#include "Creatures/Core/CreatureInventory.h"
#include "Game/WorldGameMode.h"

#include <string>

namespace cutum
{

/// Centralized consume/drop rules for survival economy.
/// Creative mode should be treated as "free" at this layer.
struct ResourceEconomyService
{
  /// True when the active entry is available for placement in current mode.
  static bool CanPlace(WorldGameMode mode,
                        const UCreatureInventory &inv,
                        const InventoryEntryRef &activeEntry);

  /// Consumes one unit from inventory/storage if allowed.
  /// Returns false when placement should be rejected.
  static bool ConsumeOnPlace(WorldGameMode mode, UCreatureInventory &inv,
                              const InventoryEntryRef &activeEntry);

  /// Grants a single dropped block into inventory.
  static void GrantBlockDrop(WorldGameMode mode, UCreatureInventory &inv,
                              const std::string &blockTypeName,
                              int count = 1);
};

} // namespace cutum

#endif

