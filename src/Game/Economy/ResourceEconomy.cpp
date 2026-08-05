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

  // Clear active slot if it was using the consumed stack.
  if (inv.GetStorage().find(activeEntry.Id) == inv.GetStorage().end())
  {
    const size_t bar = inv.GetActiveBarIndex();
    const size_t slot = inv.GetActiveSlotIndex();

    if (bar < inv.GetHotbarCount() && slot < 10)
    {
      if (auto *activeRef = inv.GetActiveEntryRef())
      {
        if (activeRef->kind == activeEntry.kind &&
            activeRef->Id == activeEntry.Id)
        {
          inv.ClearHotbarSlot(bar, slot);
        }
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
}

} // namespace cutum

