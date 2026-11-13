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

  return ConsumeUnits(mode, inv, activeEntry.kind, activeEntry.Id, 1);
}

bool ResourceEconomyService::ConsumeUnits(WorldGameMode mode,
                                          UCreatureInventory &inv,
                                          InventoryEntryKind kind,
                                          const std::string &itemId,
                                          int units)
{
  if (itemId.empty() || units <= 0)
  {
    return false;
  }

  if (!ModePolicy::ConsumesResourcesOnPlace(mode))
  {
    return true; // Creative: no consume.
  }

  // Unlimited (-1) hand stack: the copy in hand never runs out, so neither
  // storage nor mirrors are touched.
  if (const InventoryEntryRef *active = inv.GetActiveEntryRef();
      active && !active->empty && active->kind == kind &&
      active->Id == itemId && active->count < 0)
  {
    return true;
  }

  auto &storage = inv.GetStorageMutable();
  const auto it = storage.find(itemId);
  int next = 0;
  bool backed = false;
  if (it != storage.end())
  {
    // Defensive: if storage is accidentally unlimited (-1), do not modify.
    if (it->second < 0)
    {
      return true;
    }
    if (it->second < units)
    {
      return false; // storage cannot back the request; consume nothing
    }
    next = it->second - units;
    backed = true;
    if (next <= 0)
    {
      storage.erase(it);
    }
    else
    {
      it->second = next;
    }
  }

  // Sync every hotbar mirror of this stack, not just the active slot, so the
  // HUD stays true when the same item is held in several slots.
  auto &bars = inv.GetHotbarsMutable();
  for (size_t bi = 0; bi < bars.size(); ++bi)
  {
    for (size_t si = 0; si < bars[bi].slots.size(); ++si)
    {
      auto &slot = bars[bi].slots[si];
      if (slot.empty || slot.entry.kind != kind || slot.entry.Id != itemId)
      {
        continue;
      }
      if (!backed)
      {
        // Legacy ghost mirror without storage: consume from the slot copy.
        if (slot.entry.count < 0)
        {
          continue;
        }
        if (slot.entry.count > units)
        {
          slot.entry.count -= units;
        }
        else
        {
          inv.ClearHotbarSlot(bi, si);
        }
      }
      else if (next <= 0)
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

void ResourceEconomyService::GrantItemDrop(WorldGameMode mode,
                                            UCreatureInventory &inv,
                                            const std::string &itemId,
                                            int count)
{
  GrantBlockDrop(mode, inv, itemId, count, InventoryEntryKind::Item);
}

void ResourceEconomyService::GrantBlockDrop(WorldGameMode mode,
                                             UCreatureInventory &inv,
                                             const std::string &blockTypeName,
                                             int count,
                                             InventoryEntryKind kind)
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
          slot.entry.kind != kind)
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
    entry.kind = kind;
    entry.Id = blockTypeName;
    entry.count = total;
    inv.AssignToHotbar(inv.GetActiveBarIndex(), inv.GetActiveSlotIndex(),
                       entry);
  }
}

} // namespace cutum

