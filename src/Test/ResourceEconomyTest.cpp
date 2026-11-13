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

  // Item drops follow the same Creative gate.
  {
    UCreatureInventory inv;
    ResourceEconomyService::GrantItemDrop(WorldGameMode::Creative, inv,
                                           "raw_meat", 1);
    Expect(inv.GetStorage().count("raw_meat") == 0, "creative item drop skipped");
    inv.EnsureDefaultHotbar();
    ResourceEconomyService::GrantItemDrop(WorldGameMode::Survival, inv,
                                           "raw_meat", 1);
    Expect(inv.GetStorage().at("raw_meat") == 1, "survival item drop granted");
    const InventoryEntryRef *active = inv.GetActiveEntryRef();
    Expect(active && active->kind == InventoryEntryKind::Item &&
               active->Id == "raw_meat",
           "empty hand receives item kind");
  }

  // ConsumeUnits: every hotbar mirror of the same stack stays in sync.
  {
    UCreatureInventory inv;
    inv.EnsureHotbarCount(1);
    inv.GetStorageMutable()["torch"] = 5;
    InventoryEntryRef a;
    a.kind = InventoryEntryKind::Item;
    a.Id = "torch";
    a.empty = false;
    a.count = 5;
    InventoryEntryRef b = a;
    inv.AssignToHotbar(0, 0, a);
    inv.AssignToHotbar(0, 1, b);
    inv.SetActiveBarIndex(0);
    inv.SetActiveSlotIndex(0);

    Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                                InventoryEntryKind::Item,
                                                "torch", 1),
           "consume with two mirrors succeeds");
    Expect(inv.GetStorage().at("torch") == 4, "storage decremented once");
    const auto &slots = inv.GetHotbar(0).slots;
    Expect(slots.size() > 1 && slots[0].entry.count == 4 &&
               slots[1].entry.count == 4,
           "both mirrors show the new total");
  }

  // ConsumeUnits: an unlimited (-1) hand stack consumes nothing.
  {
    UCreatureInventory inv;
    inv.EnsureHotbarCount(1);
    inv.GetStorageMutable()["torch"] = 5;
    InventoryEntryRef a;
    a.kind = InventoryEntryKind::Item;
    a.Id = "torch";
    a.empty = false;
    a.count = -1;
    inv.AssignToHotbar(0, 0, a);
    inv.SetActiveBarIndex(0);
    inv.SetActiveSlotIndex(0);
    Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                                InventoryEntryKind::Item,
                                                "torch", 1),
           "unlimited hand consumes without error");
    Expect(inv.GetStorage().at("torch") == 5, "unlimited hand keeps storage");
    Expect(inv.GetHotbar(0).slots[0].entry.count == -1,
           "unlimited hand stack kept");
  }

  // ConsumeUnits: storage must be able to back the request.
  {
    UCreatureInventory inv;
    inv.EnsureHotbarCount(1);
    inv.GetStorageMutable()["torch"] = 1;
    InventoryEntryRef a;
    a.kind = InventoryEntryKind::Item;
    a.Id = "torch";
    a.empty = false;
    a.count = 1;
    inv.AssignToHotbar(0, 0, a);
    inv.SetActiveBarIndex(0);
    inv.SetActiveSlotIndex(0);
    Expect(!ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                                 InventoryEntryKind::Item,
                                                 "torch", 2),
           "consuming more than stored is rejected");
    Expect(inv.GetStorage().at("torch") == 1,
           "rejected consume keeps storage untouched");
  }

  // ConsumeUnits: legacy ghost mirror without storage consumes slot copies.
  {
    UCreatureInventory inv;
    inv.EnsureHotbarCount(1);
    InventoryEntryRef a;
    a.kind = InventoryEntryKind::Item;
    a.Id = "legacy_item";
    a.empty = false;
    a.count = 2;
    inv.AssignToHotbar(0, 0, a);
    inv.SetActiveBarIndex(0);
    inv.SetActiveSlotIndex(0);
    Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                                InventoryEntryKind::Item,
                                                "legacy_item", 1),
           "ghost mirror consume succeeds");
    Expect(inv.GetHotbar(0).slots[0].entry.count == 1,
           "ghost slot copy decremented");
    Expect(ResourceEconomyService::ConsumeUnits(WorldGameMode::Survival, inv,
                                                InventoryEntryKind::Item,
                                                "legacy_item", 1),
           "last ghost copy consumes");
    Expect(inv.GetHotbar(0).slots[0].empty, "ghost slot cleared at zero");
  }

  std::cout << "resource_economy_test: OK" << std::endl;
  return 0;
}

