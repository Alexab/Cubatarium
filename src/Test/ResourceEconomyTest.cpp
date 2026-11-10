#include "Game/Economy/ResourceEconomy.h"

#include "Creatures/Core/CreatureInventory.h"
#include "Game/WorldGameMode.h"

#include <iostream>

using namespace cutum;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main()
{
  // Creative: placement is always allowed and nothing is consumed/dropped.
  {
    UCreatureInventory inv;
    inv.InitCreativeDefaults();

    InventoryEntryRef wood;
    wood.kind = InventoryEntryKind::Block;
    wood.Id = "wood";
    wood.empty = false;
    wood.count = 1;

    Expect(ResourceEconomyService::CanPlace(WorldGameMode::Creative, inv, wood),
           "creative can place");

    const int before = inv.GetStorage().at("wood");
    const bool consumed =
        ResourceEconomyService::ConsumeOnPlace(WorldGameMode::Creative, inv,
                                                  wood);
    Expect(consumed, "creative consume returns true");
    const int after = inv.GetStorage().at("wood");
    Expect(before == after && after < 0, "creative keeps unlimited storage");

    ResourceEconomyService::GrantBlockDrop(WorldGameMode::Creative, inv,
                                             "wood", 1);
    const int afterDrop = inv.GetStorage().at("wood");
    Expect(afterDrop < 0, "creative no drop");
  }

  // Survival: placement consumes storage and drops grant back on break.
  {
    UCreatureInventory inv;
    inv.EnsureHotbarCount(1);
    inv.SetActiveBarIndex(0);
    inv.SetActiveSlotIndex(1);

    InventoryEntryRef wood;
    wood.kind = InventoryEntryKind::Block;
    wood.Id = "wood";
    wood.empty = false;
    wood.count = 1;
    inv.AssignToHotbar(0, 1, wood);

    inv.GetStorageMutable()["wood"] = 2;

    Expect(ResourceEconomyService::CanPlace(WorldGameMode::Survival, inv, wood),
           "survival can place when storage > 0");

    const bool ok1 = ResourceEconomyService::ConsumeOnPlace(WorldGameMode::Survival,
                                                              inv, wood);
    Expect(ok1, "consume #1 succeeds");
    Expect(inv.GetStorage().at("wood") == 1, "storage decremented to 1");

    const bool ok2 = ResourceEconomyService::ConsumeOnPlace(WorldGameMode::Survival,
                                                              inv, wood);
    Expect(ok2, "consume #2 succeeds");
    Expect(inv.GetStorage().find("wood") == inv.GetStorage().end(),
           "storage key removed when reaches 0");
    Expect(inv.GetActiveEntryRef() == nullptr, "active slot cleared at 0");

    ResourceEconomyService::GrantBlockDrop(WorldGameMode::Survival, inv,
                                             "wood", 1);
    Expect(inv.GetStorage().at("wood") == 1, "break drop grants block");
  }

  std::cout << "resource_economy_test: OK" << std::endl;
  return 0;
}

