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
void ResourceEconomyService::GrantBlockDrop(WorldGameMode mode,
                                            UCreatureInventory &inv,
                                            const std::string &id, int count,
                                            InventoryEntryKind kind)
{
  if (!ModePolicy::DropsBlocksOnBreak(mode) || id.empty() || count <= 0)
    return;
  InventoryEntryRef e;
  e.kind = kind;
  e.Id = id;
  e.count = count;
  e.empty = false;
  (void)inv.Insert(
      e); // world pickup retains any remainder; see WorldItemSystem
}
void ResourceEconomyService::GrantItemDrop(WorldGameMode mode,
                                           UCreatureInventory &inv,
                                           const std::string &id, int count)
{
  GrantBlockDrop(mode, inv, id, count, InventoryEntryKind::Item);
}
} // namespace cutum
