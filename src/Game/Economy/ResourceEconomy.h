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

  /// Consumes `units` of `itemId` (of `kind`) from storage and re-syncs every
  /// hotbar mirror so HUD counts stay true when the item is held in several
  /// slots. Creative consumes nothing and returns true; an unlimited (-1)
  /// hand stack or storage entry stays untouched. Without any storage backing
  /// (legacy ghost mirror) only the slot copies are consumed. Returns false
  /// only when storage exists but cannot back the request — callers must not
  /// apply the use effect in that case.
  static bool ConsumeUnits(WorldGameMode mode, UCreatureInventory &inv,
                           InventoryEntryKind kind, const std::string &itemId,
                           int units = 1);

  /// Grants a dropped stack. `kind` selects the hotbar entry written into an empty hand.
  static void GrantBlockDrop(WorldGameMode mode, UCreatureInventory &inv,
                              const std::string &blockTypeName,
                              int count = 1,
                              InventoryEntryKind kind = InventoryEntryKind::Block);

  /// Grants a creature/item drop into inventory (same Creative gate as blocks).
  static void GrantItemDrop(WorldGameMode mode, UCreatureInventory &inv,
                             const std::string &itemId, int count = 1);
};

} // namespace cutum

#endif

