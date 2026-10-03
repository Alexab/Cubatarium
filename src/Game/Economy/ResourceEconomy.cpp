#include "Game/Economy/ResourceEconomy.h"

#include "Game/ModePolicy.h"

namespace cutum
{

namespace
{

bool IsStorageBackedKind(const InventoryEntryRef &entry)
{
  return entry.kind == InventoryEntryKind::Block ||
         entry.kind == InventoryEntryKind::Object;
}

} // namespace

bool ResourceEconomyService::CanPlace(WorldGameMode mode,
                                       const UCreatureInventory &inv,
                                       const InventoryEntryRef &activeEntry)
{
  if (activeEntry.empty || activeEntry.Id.empty() ||
      !IsStorageBackedKind(activeEntry))
  {
    return false;
  }

  if (!ModePolicy::ConsumesResourcesOnPlace(mode))
  {
    // Creative/freedom: placement is always allowed at this layer.
    return true;
  }

  const auto &storage = inv.GetStorage();
  const auto it = storage.find(activeEntry.Id);
  if (it == storage.end())
  {
    return false;
  }

  // Survival storage should be non-negative, but keep the rule robust.
  return it->second > 0;
}

bool ResourceEconomyService::ConsumeOnPlace(WorldGameMode mode,
                                            UCreatureInventory &inv,
                                            const InventoryEntryRef &activeEntry)
{
  if (activeEntry.empty || activeEntry.Id.empty() ||
      !IsStorageBackedKind(activeEntry))
  {
    return false;
  }

  if (!ModePolicy::ConsumesResourcesOnPlace(mode))
  {
    return true; // Creative: no consume.
  }

  auto &storage = inv.GetStorageMutable();
  const auto it = storage.find(activeEntry.Id);
  if (it == storage.end() || it->second <= 0)
  {
    return false;
  }

  // Defensive: if storage is accidentally unlimited (-1), do not modify.
  if (it->second < 0)
  {
    return true;
  }

  const int next = it->second - 1;
  if (next <= 0)
  {
    storage.erase(it);
  }
  else
  {
    it->second = next;
  }

  // Sync hotbar counts for matching stacks.
  auto &bars = inv.GetHotbarsMutable();
  for (size_t bi = 0; bi < bars.size(); ++bi)
  {
    for (size_t si = 0; si < bars[bi].slots.size(); ++si)
    {
      auto &slot = bars[bi].slots[si];
      if (slot.empty || slot.entry.kind != activeEntry.kind ||
          slot.entry.Id != activeEntry.Id)
      {
        continue;
      }
      if (next <= 0)
      {
        inv.ClearHotbarSlot(bi, si);
      }
      else
      {
        slot.entry.count = next;
      }
    }
  }

  return true;
}

void ResourceEconomyService::GrantBlockDrop(WorldGameMode mode,
                                             UCreatureInventory &inv,
                                             const std::string &blockTypeName,
                                             int count)
{
  if (blockTypeName.empty() || count <= 0)
  {
    return;
  }
  if (!ModePolicy::DropsBlocksOnBreak(mode))
  {
    return; // Creative: no drops.
  }

  // Preserve unlimited (-1) storage for any unexpected creative residue.
  auto &storage = inv.GetStorageMutable();
  const auto it = storage.find(blockTypeName);
  if (it != storage.end() && it->second < 0)
  {
    return;
  }

  inv.AddItem(blockTypeName, count);

  // Keep hotbar stack counts in sync so the HUD reflects backpack totals.
  const auto storageIt = inv.GetStorage().find(blockTypeName);
  const int total =
      (storageIt == inv.GetStorage().end()) ? 0 : storageIt->second;
  if (total <= 0)
  {
    return;
  }

  auto &bars = inv.GetHotbarsMutable();
  bool syncedExisting = false;
  for (auto &bar : bars)
  {
    for (auto &slot : bar.slots)
    {
      if (slot.empty || slot.entry.Id != blockTypeName ||
          slot.entry.kind != InventoryEntryKind::Block)
      {
        continue;
      }
      slot.entry.count = total;
      syncedExisting = true;
    }
  }

  // Empty hands: auto-equip the drop so place/dig loop works immediately.
  if (!syncedExisting && !inv.GetActiveEntryRef())
  {
    InventoryEntryRef entry;
    entry.empty = false;
    entry.kind = InventoryEntryKind::Block;
    entry.Id = blockTypeName;
    entry.count = total;
    inv.AssignToHotbar(inv.GetActiveBarIndex(), inv.GetActiveSlotIndex(),
                       entry);
  }
}

} // namespace cutum

