#include "Creatures/Core/CreatureInventory.h"
#include "Game/Crafting/RecipeRegistry.h"
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
  Expect(!inv.RepairActive(defs, repairReason), "missing repair material rejected");
  Expect(repairBefore == Snapshot(inv), "failed repair has no side effects");
  inv.AddItem(defs.Get("iron_pickaxe")->Repair.Materials.front(), 1);
  Expect(inv.RepairActive(defs, repairReason), "repair consumes material atomically");
  Expect(inv.GetSlot(1).instanceId == fresh.instanceId && inv.GetSlot(1).wear == 0.f,
         "repair preserves instance identity");
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
  std::cout << "inventory_transactions_test failures=" << failures << '\n';
  return failures ? 1 : 0;
}
