#include "Game/Economy/ResourceEconomy.h"
#include "Game/ModePolicy.h"
namespace cutum
{
bool ResourceEconomyService::CanPlace(WorldGameMode mode,
                                      const UCreatureInventory &inv,
                                      const InventoryEntryRef &entry)
{
  if (entry.empty || entry.Id.empty() ||
      (entry.kind != InventoryEntryKind::Block &&
       entry.kind != InventoryEntryKind::Object))
    return false;
  return !ModePolicy::ConsumesResourcesOnPlace(mode) ||
         inv.Count(entry.kind, entry.Id) > 0;
}
bool ResourceEconomyService::ConsumeOnPlace(WorldGameMode mode,
                                            UCreatureInventory &inv,
                                            const InventoryEntryRef &entry)
{
  if (entry.empty || (entry.kind != InventoryEntryKind::Block &&
                      entry.kind != InventoryEntryKind::Object))
    return false;
  return ConsumeUnits(mode, inv, entry.kind, entry.Id);
}
bool ResourceEconomyService::ConsumeUnits(WorldGameMode mode,
                                          UCreatureInventory &inv,
                                          InventoryEntryKind kind,
                                          const std::string &id, int units)
{
  if (id.empty() || units <= 0)
    return false;
  if (!ModePolicy::ConsumesResourcesOnPlace(mode))
    return true;
  return static_cast<bool>(inv.Consume(kind, id, units));
}
} // namespace cutum
