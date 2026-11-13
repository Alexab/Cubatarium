#include "Creatures/Core/CreatureInventory.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include <cmath>
#include <iostream>
#include <set>
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
static InventoryEntryRef Item(const char *id, float wear = 0.f)
{
  InventoryEntryRef e;
  e.kind = InventoryEntryKind::Item;
  e.Id = id;
  e.count = 1;
  e.empty = false;
  e.wear = wear;
  return e;
}
int main()
{
  UItemDefinitionStorage defs;
  defs.Load(CUBATARIUM_SOURCE_DIR "/content/items");
  const auto *helmet = defs.Get("iron_head");
  Expect(helmet && helmet->Armor.Slots == std::vector<std::string>{"head"},
         "head slot parsed");
  UCreatureInventory inv;
  inv.SetItemDefinitions(&defs);
  Expect(static_cast<bool>(inv.Insert(Item("iron_head", .65f))),
         "insert worn helmet");
  Expect(static_cast<bool>(inv.Insert(Item("iron_head", .2f))),
         "insert second helmet");
  auto first = inv.GetSlot(0), second = inv.GetSlot(1);
  Expect(first.instanceId != second.instanceId, "independent identities");
  Expect(inv.EquipArmor(0, first, defs), "equip owned instance");
  Expect(inv.GetSlot(0).empty && inv.GetEquippedArmor(0).wear == .65f,
         "moves exact state out of bag");
  Expect(!inv.EquipArmor(2, second, defs), "wrong slot rejected");
  Expect(inv.EquipArmor(0, second, defs), "replace helmet atomically");
  Expect(inv.GetEquippedArmor(0).instanceId == second.instanceId,
         "correct second instance equipped");
  Expect(inv.GetSlot(0).instanceId == first.instanceId &&
             inv.GetSlot(0).wear == .65f,
         "old helmet returned unchanged");
  inv.UnequipArmor(0, defs);
  Expect(inv.GetEquippedArmor(0).empty &&
             inv.Count(InventoryEntryKind::Item, "iron_head") == 2,
         "unequip keeps both");
  Expect(inv.EquipOffhand(first, defs), "offhand moves exact instance");
  inv.UnequipOffhand(defs);
  Expect(inv.Count(InventoryEntryKind::Item, "iron_head") == 2,
         "offhand round trip conserves quantity");
  auto broken = Item("iron_head", 1.f);
  broken.broken = true;
  Expect(inv.EquipArmor(0, broken, defs, false),
         "explicit QA injects broken gear");
  Expect(inv.GetEquippedArmorGroups().Ratings.empty(),
         "broken gear has no protection");
  inv.UnequipArmor(0, defs);
  Expect(inv.Count(InventoryEntryKind::Item, "iron_head") == 3,
         "repairable broken gear is preserved");
  nlohmann::json saved;
  inv.SerializeToJson(saved);
  UCreatureInventory restored;
  restored.SetItemDefinitions(&defs);
  restored.DeserializeFromJson(saved);
  nlohmann::json again;
  restored.SerializeToJson(again);
  Expect(saved == again, "complete state round trip");
  UCreatureInventory full;
  full.SetItemDefinitions(&defs);
  full.EquipArmor(0, first, defs, false);
  InventoryEntryRef stone;
  stone.Id = "stone";
  stone.kind = InventoryEntryKind::Block;
  stone.count = 64;
  stone.empty = false;
  for (int i = 0; i < 40; ++i)
    Expect(static_cast<bool>(full.Insert(stone)), "fill physical bag");
  auto equipped = full.GetEquippedArmor(0);
  full.UnequipArmor(0, defs);
  Expect(full.GetEquippedArmor(0).instanceId == equipped.instanceId,
         "full bag rejects unequip without loss");
  Expect(!full.EquipArmor(0, Item("gold_head"), defs),
         "unowned equip rejected");
  return failures ? 1 : 0;
}
