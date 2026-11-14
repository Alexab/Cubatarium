#include "Creatures/Core/CreatureInventory.h"
#include "Game/Crafting/RecipeRegistry.h"
#include "Game/Inventory/InventoryOperations.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
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
static InventoryEntryRef Item(const char *id, int count = 1, float wear = 0.f)
{
  InventoryEntryRef e;
  e.kind = InventoryEntryKind::Item;
  e.Id = id;
  e.count = count;
  e.empty = false;
  e.wear = wear;
  return e;
}
static nlohmann::json Snapshot(const UCreatureInventory &i)
{
  nlohmann::json j;
  i.SerializeToJson(j);
  return j;
}
int main()
{
  UItemDefinitionStorage defs;
  defs.Load(CUBATARIUM_SOURCE_DIR "/content/items");
  UCreatureInventory inv;
  inv.SetItemDefinitions(&defs);
  inv.Insert(Item("iron_pickaxe", 1, .8f));
  inv.Insert(Item("iron_pickaxe", 1, .1f));
  const auto old = inv.GetSlot(0), fresh = inv.GetSlot(1);
  Expect(old.instanceId != fresh.instanceId, "durable instances are unique");
  Expect(inv.AssignOwned(2, 3, old), "assign moves exact tool");
  Expect(inv.GetSlot(23).instanceId == old.instanceId &&
             inv.GetSlot(23).wear == .8f,
         "assignment preserves damage");
  Expect(inv.GetSlot(0).empty && inv.GetSlot(1).instanceId == fresh.instanceId,
         "assignment never creates mirror");
  auto forged = old;
  forged.instanceId += 1000;
  Expect(!inv.AssignOwned(0, 0, forged), "stale identity rejected");
  inv.SetActiveSlot(2, 3);
  auto *tool = inv.GetActiveEntryRef();
  Expect(ApplyItemWear(*tool, *defs.Get(tool->Id), 1.f, true),
         "destruction applied");
  Expect(inv.Count(old.kind, old.Id) == 1 && !inv.AssignOwned(0, 0, old),
         "broken tool cannot be resurrected");
  const auto before = Snapshot(inv);
  Expect(!inv.Move(1, 2, 20), "oversized split rejected");
  Expect(before == Snapshot(inv), "rejected split is atomic");
  auto apples = Item("apple", 35);
  const auto inserted = inv.Insert(apples);
  Expect(static_cast<bool>(inserted), "stack overflow splits by definition");
  for (size_t s = 0; s < 40; ++s)
    if (inv.GetSlot(s).Id == "apple")
      Expect(inv.GetSlot(s).count <= 16, "stack_max enforced");
  URecipeRegistry recipes;
  RecipeDefinition duplicate;
  duplicate.Id = "duplicate";
  duplicate.Inputs = {{"apple", 20}, {"apple", 20}};
  duplicate.Output = {"bread", 1};
  inv.SetActiveSlot(0, 1);
  std::string repairReason;
  const auto repairBefore = Snapshot(inv);
  Expect(!inv.RepairActive(defs, repairReason),
         "missing repair material rejected");
  Expect(repairBefore == Snapshot(inv), "failed repair has no side effects");
  inv.AddItem(defs.Get("iron_pickaxe")->Repair.Materials.front(), 1);
  Expect(inv.RepairActive(defs, repairReason),
         "repair consumes material atomically");
  Expect(inv.GetSlot(1).instanceId == fresh.instanceId &&
             inv.GetSlot(1).wear == 0.f,
         "repair preserves instance identity");
  UCreatureInventory otherOwner;
  otherOwner.SetItemDefinitions(&defs);
  otherOwner.Insert(Item("iron_pickaxe"));
  Expect(otherOwner.GetSlot(0).instanceId != fresh.instanceId,
         "instance identity is unique across owners");
  const auto preCraft = Snapshot(inv);
  Expect(!recipes.TryCraft(inv, duplicate),
         "aggregate duplicate ingredient costs");
  Expect(preCraft == Snapshot(inv), "failed craft unchanged");
  RecipeDefinition craft;
  craft.Id = "test";
  craft.Inputs = {{"apple", 2}};
  craft.Output = {"bread", 1};
  Expect(recipes.TryCraft(inv, craft), "atomic valid craft");
  Expect(inv.Count(InventoryEntryKind::Item, "apple") == 33 &&
             inv.Count(InventoryEntryKind::Item, "bread") == 1,
         "craft accounting");
  UCreatureInventory full;
  full.SetItemDefinitions(&defs);
  InventoryEntryRef b;
  b.kind = InventoryEntryKind::Block;
  b.Id = "stone";
  b.count = 64;
  b.empty = false;
  for (int s = 0; s < 40; ++s)
    full.Insert(b);
  const auto fullSnapshot = Snapshot(full);
  Expect(!full.Insert(Item("bread")), "capacity refusal");
  Expect(fullSnapshot == Snapshot(full), "failed full insert unchanged");
  nlohmann::json legacy = {
      {"storage", {{"iron_pickaxe", 1}, {"wood", 130}}},
      {"hotbars",
       nlohmann::json::array({nlohmann::json::array({{{"id", "iron_pickaxe"},
                                                      {"kind", "item"},
                                                      {"count", 1},
                                                      {"wear", .2f}},
                                                     {{"id", "iron_pickaxe"},
                                                      {"kind", "item"},
                                                      {"count", 1},
                                                      {"wear", .9f}}})})}};
  UCreatureInventory migrated;
  migrated.SetItemDefinitions(&defs);
  migrated.DeserializeFromJson(legacy);
  Expect(migrated.Count(InventoryEntryKind::Item, "iron_pickaxe") == 1,
         "legacy mirrors don't duplicate");
  Expect(migrated.GetSlot(0).wear == .9f &&
             !migrated.GetMigrationWarnings().empty(),
         "migration preserves worst ambiguous wear");
  Expect(migrated.Count(InventoryEntryKind::Block, "wood") == 130,
         "migration preserves resource total");
  auto j = Snapshot(migrated);
  UCreatureInventory twice;
  twice.SetItemDefinitions(&defs);
  twice.DeserializeFromJson(j);
  Expect(j == Snapshot(twice), "migration idempotent");
  migrated.SetCreative(true);
  migrated.InitCreativeDefaults();
  migrated.SetCreative(false);
  Expect(migrated.Count(InventoryEntryKind::Item, "iron_pickaxe") == 1,
         "mode round trip restores belongings");
  UCreatureInventory gestures;
  gestures.SetItemDefinitions(&defs);
  gestures.Insert(Item("apple", 24));
  gestures.Move(0, 20, 4);
  gestures.CollectMatching(20);
  Expect(gestures.GetSlot(20).count == 16 &&
             gestures.Count(InventoryEntryKind::Item, "apple") == 24,
         "collect respects stack limit and conserves count");
  gestures.SetActiveSlot(0, 0);
  gestures.QuickMove(0);
  Expect(gestures.Count(InventoryEntryKind::Item, "apple") == 24 &&
             gestures.GetSlot(0).empty,
         "quick transfer conserves ownership");
  RecipeDefinition station = craft;
  station.Station = "furnace";
  const auto stationBefore = Snapshot(inv);
  Expect(!recipes.TryCraft(inv, station) && Snapshot(inv) == stationBefore,
         "station requirement is atomic");
  UCreatureInventory creativeFinite;
  creativeFinite.SetItemDefinitions(&defs);
  creativeFinite.SetCreative(true);
  auto catalog = Item("apple");
  catalog.form = InventoryEntryForm::CatalogReference;
  creativeFinite.AssignToHotbar(0, 0, catalog);
  creativeFinite.Insert(Item("apple", 8));
  InventoryEntryRef extracted;
  Expect(creativeFinite.Take(1, 3, extracted) && extracted.count == 3 &&
             !extracted.IsCatalogReference() &&
             creativeFinite.GetSlot(1).count == 5,
         "creative finite extraction transfers rather than copies");
  Expect(creativeFinite.Take(0, 3, extracted) && extracted.count == 3 &&
             !extracted.IsCatalogReference() &&
             creativeFinite.GetSlot(0).IsCatalogReference(),
         "catalog materialization preserves the reference");
  Expect(creativeFinite.Move(1, 2, 2) && creativeFinite.GetSlot(1).count == 3 &&
             creativeFinite.GetSlot(2).count == 2,
         "creative finite splitting conserves quantities");
  auto creativeSave = Snapshot(creativeFinite);
  UCreatureInventory creativeLoaded;
  creativeLoaded.SetItemDefinitions(&defs);
  creativeLoaded.DeserializeFromJson(creativeSave);
  Expect(creativeLoaded.GetSlot(0).IsCatalogReference() &&
             creativeLoaded.GetSlot(1).count == 3 &&
             creativeLoaded.GetSlot(2).count == 2,
         "v4 roundtrip retains mixed creative entries");
  UCreatureInventory creativeGear;
  creativeGear.SetItemDefinitions(&defs);
  creativeGear.SetCreative(true);
  creativeGear.Insert(Item("iron_chest", 1, .6f));
  const auto gearEntry = creativeGear.GetSlot(0);
  Expect(creativeGear.EquipArmor(1, gearEntry, defs) &&
             creativeGear.GetSlot(0).empty &&
             creativeGear.GetEquippedArmor(1).instanceId ==
                 gearEntry.instanceId,
         "creative equipment moves the finite instance");
  creativeGear.UnequipArmor(1, defs);
  Expect(creativeGear.GetEquippedArmor(1).empty &&
             creativeGear.GetSlot(0).instanceId == gearEntry.instanceId &&
             creativeGear.GetSlot(0).wear == .6f,
         "creative unequip returns the same worn instance");
  auto oldCreative = creativeSave;
  oldCreative["inventory_version"] = 3;
  oldCreative["creative_slots"][0].erase("form");
  oldCreative["creative_slots"][0]["count"] = -1;
  creativeLoaded.DeserializeFromJson(oldCreative);
  Expect(creativeLoaded.GetSlot(0).IsCatalogReference() &&
             creativeLoaded.GetSlot(0).count == 1 &&
             creativeLoaded.GetSlot(1).count == 3,
         "v3 negative catalog counts migrate without converting finite stock");
  const auto beforeTransfer = Snapshot(creativeFinite);
  auto rejected = UInventoryOperations::TransferToWorld(
      creativeFinite, 1, 1, [](const InventoryEntryRef &) { return false; });
  Expect(!rejected && Snapshot(creativeFinite) == beforeTransfer,
         "world rejection preserves original inventory");
  auto revision = creativeFinite.GetRevision();
  Expect(UInventoryOperations::Matches(creativeFinite, revision, true),
         "current revision validates");
  creativeFinite.Take(1, 1, extracted);
  Expect(!UInventoryOperations::Matches(creativeFinite, revision, true),
         "stale revision rejects transfer");
  creativeFinite.AddItem("unknown-mod-item", 1);
  // Overflow is domain-local even when there are no free slots.
  UCreatureInventory recoveries;
  recoveries.SetItemDefinitions(&defs);
  recoveries.SetCreative(true);
  for (int i = 0; i < 40; ++i)
    recoveries.Insert(Item("iron_pickaxe"));
  recoveries.AddItem("iron_pickaxe");
  Expect(recoveries.GetRecovery().size() == 1, "creative overflow recorded");
  recoveries.SetCreative(false);
  Expect(recoveries.GetRecovery().empty(),
         "survival cannot recover creative overflow");
  auto recoverySave = Snapshot(recoveries);
  recoveries.DeserializeFromJson(recoverySave);
  recoveries.SetCreative(true);
  Expect(recoveries.GetRecovery().size() == 1,
         "creative recovery persists across mode and load");
  UCreatureInventory partial;
  partial.SetItemDefinitions(&defs);
  for (int i = 0; i < 39; ++i)
    partial.Insert(Item("iron_pickaxe"));
  InventoryEntryRef stone;
  stone.empty = false;
  stone.kind = InventoryEntryKind::Block;
  stone.Id = "stone";
  stone.count = 63;
  partial.Insert(stone);
  auto partialSave = Snapshot(partial);
  auto overflow = partialSave["owned_slots"][39];
  overflow["count"] = 2;
  overflow["instance_id"] = 0;
  partialSave["inventory_recovery"].push_back(overflow);
  partial.DeserializeFromJson(partialSave);
  UCreatureInventory recoveryOtherOwner;
  recoveryOtherOwner.SetItemDefinitions(&defs);
  stone.count = 1;
  recoveryOtherOwner.Insert(stone);
  const auto recovered = partial.Recover(0);
  Expect(recovered.transferred == 1 && recovered.remainder == 1 &&
             partial.GetRecovery()[0].instanceId !=
                 recoveryOtherOwner.GetSlot(0).instanceId,
         "partial recovery uses globally unique remainder identity");
  Expect(defs.Get("wood_bow")->ThrowClass == ThrowCategory::Weapon &&
             defs.Get("wood_spear")->ThrowClass == ThrowCategory::Weapon &&
             defs.Get("wood_torch")->ThrowClass == ThrowCategory::Utility &&
             defs.Get("apple")->ThrowClass == ThrowCategory::Food,
         "throw roles classify existing weapons utilities and food "
         "independently of catalog tabs");
  std::cout << "inventory_transactions_test failures=" << failures << '\n';
  return failures ? 1 : 0;
}
