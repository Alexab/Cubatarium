#include "Game/Economy/ResourceEconomy.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Items/ItemDefinitionStorage.h"
#include <iostream>
using namespace cutum;
static int failures = 0;
static void Expect(bool b, const char *m)
{
  if (!b)
  {
    std::cerr << "FAIL: " << m << '\n';
    ++failures;
  }
}
int main()
{
  UItemDefinitionStorage defs;
  defs.Load(CUBATARIUM_SOURCE_DIR "/content/items");
  UCreatureInventory inv;
  inv.SetItemDefinitions(&defs);
  InventoryEntryRef block;
  block.kind = InventoryEntryKind::Block;
  block.Id = "wood";
  block.count = 5;
  block.empty = false;
  Expect(static_cast<bool>(inv.Insert(block)), "grant five real blocks");
  Expect(static_cast<bool>(inv.Move(0, 1, 2)), "split into two real stacks");
  inv.SetActiveSlot(0, 1);
  Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                              block.kind, block.Id),
         "consume active physical stack");
  Expect(inv.GetSlot(0).count == 3 && inv.GetSlot(1).count == 1,
         "other stack is not a mirror");
  Expect(!ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                               block.kind, block.Id, 5),
         "over-consume rejected");
  Expect(inv.Count(block.kind, block.Id) == 4, "failure preserves stock");
  Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                              block.kind, block.Id, 4),
         "consume across stacks atomically");
  Expect(inv.GetActiveEntryRef() == nullptr &&
             inv.Count(block.kind, block.Id) == 0,
         "last physical unit disappears");
  Expect(!ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                               block.kind, block.Id),
         "missing source cannot consume successfully");
  InventoryEntryRef meat{InventoryEntryKind::Item, "raw_meat", 2, false};
  inv.Insert(meat);
  Expect(inv.Count(InventoryEntryKind::Item, "raw_meat") == 2,
         "finite item seed is separate from block stock");
  inv.SetCreative(true);
  inv.InitCreativeDefaults();
  inv.SetActiveSlot(0, 1);
  Expect(ResourceEconomyService::ConsumeOnPlace(WorldGameMode::Creative, inv,
                                                block),
         "creative free placement");
  Expect(inv.GetSlot(1).IsCatalogReference(),
         "creative loadout remains unlimited");
  inv.SetCreative(false);
  Expect(inv.Count(InventoryEntryKind::Item, "raw_meat") == 2 &&
             inv.Count(block.kind, block.Id) == 0,
         "creative does not contaminate owned stock");
  return failures ? 1 : 0;
}
