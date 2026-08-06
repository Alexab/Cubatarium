#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>

using namespace cutum;

static int Failures = 0;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    ++Failures;
  }
}

int main()
{
  namespace fs = std::filesystem;

  const fs::path tmp = fs::temp_directory_path() / "cubatarium_armor_equip_test";
  const fs::path itemsDir = tmp / "items";
  fs::create_directories(itemsDir);

  const fs::path helmetJson = itemsDir / "armor_helmet_test.json";
  {
    std::ofstream out(helmetJson);
    out << R"({
  "id": "armor_helmet_test",
  "displayName": "Test Helmet",
  "wear_end": "destroy",
  "armor": {
    "slots": ["head"],
    "armor_groups": { "fleshy": 100 }
  }
})";
  }

  UItemDefinitionStorage storage;
  storage.Load(itemsDir.string());

  // 1) Parsing test.
  const ItemDefinition *helmet = storage.Get("armor_helmet_test");
  Expect(helmet != nullptr, "armor definition present");
  if (helmet)
  {
    Expect(helmet->Armor.Slots.size() == 1 &&
               helmet->Armor.Slots[0] == "head",
           "armor slots parsed");
    const auto it = helmet->Armor.ArmorGroups.find("fleshy");
    Expect(it != helmet->Armor.ArmorGroups.end() && it->second == 100,
           "armor_groups fleshy parsed");
  }

  // 2) Equipment → armor_groups → ResolveHitParams influence test.
  // Use base armor where fleshy starts at 0 so we can clearly see scaling.
  ArmorGroups base;
  base.Ratings["fleshy"] = 0;

  ToolCapabilitiesDef tool;
  tool.FullPunchInterval = 1.0f;
  tool.PunchAttackUses = 0;
  tool.Damage.Groups["fleshy"] = 10;

  CreatureAttributes attrs;
  attrs.accuracy = 20; // no miss

  HitParams before = ResolveHitParams(base, tool, attrs, 1.0f);
  Expect(!before.DidHit && !before.Missed, "zero armor means zero damage");

  // Equip helmet into slot 0 (head).
  UCreatureInventory inv;
  InventoryEntryRef entry;
  entry.empty = false;
  entry.kind = InventoryEntryKind::Item;
  entry.Id = "armor_helmet_test";
  entry.count = 1;
  entry.wear = 0.f;
  entry.broken = false;

  const bool ok = inv.EquipArmor(0, entry, storage);
  Expect(ok, "EquipArmor succeeds for valid head helmet");

  ArmorGroups total = base;
  for (const auto &pair : inv.GetEquippedArmorGroups().Ratings)
  {
    total.Ratings[pair.first] += pair.second;
  }

  HitParams after = ResolveHitParams(total, tool, attrs, 1.0f);
  Expect(after.DidHit && !after.Missed, "equipped armor enables hit damage");
  Expect(std::fabs(after.Damage - 10.f) < 0.05f,
         "damage scales with armor rating (100/100)");

  // Equip same helmet but broken: should not contribute to armor_groups.
  inv.UnequipArmor(0, storage);
  entry.broken = true;
  const bool okBroken = inv.EquipArmor(0, entry, storage);
  Expect(okBroken, "EquipArmor accepts broken entries (but should skip rating)");

  ArmorGroups totalBroken = base;
  for (const auto &pair : inv.GetEquippedArmorGroups().Ratings)
  {
    totalBroken.Ratings[pair.first] += pair.second;
  }

  HitParams afterBroken = ResolveHitParams(totalBroken, tool, attrs, 1.0f);
  Expect(!afterBroken.DidHit && !afterBroken.Missed,
         "broken armor should not contribute to damage");

  // 3) Offhand equip + JSON round-trip.
  {
    UCreatureInventory ohInv;
    InventoryEntryRef empty;
    Expect(!ohInv.EquipOffhand(empty), "EquipOffhand rejects empty");

    InventoryEntryRef toolEntry;
    toolEntry.empty = false;
    toolEntry.kind = InventoryEntryKind::Item;
    toolEntry.Id = "iron_pickaxe";
    toolEntry.count = 1;
    toolEntry.wear = 0.25f;
    toolEntry.broken = false;
    Expect(ohInv.EquipOffhand(toolEntry), "EquipOffhand accepts Item");
    Expect(!ohInv.GetEquippedOffhand().empty &&
               ohInv.GetEquippedOffhand().Id == "iron_pickaxe" &&
               std::fabs(ohInv.GetEquippedOffhand().wear - 0.25f) < 1e-4f,
           "offhand item stored");

    InventoryEntryRef blockEntry;
    blockEntry.empty = false;
    blockEntry.kind = InventoryEntryKind::Block;
    blockEntry.Id = "wood";
    blockEntry.count = -1;
    Expect(ohInv.EquipOffhand(blockEntry), "EquipOffhand accepts Block");
    Expect(ohInv.GetEquippedOffhand().kind == InventoryEntryKind::Block &&
               ohInv.GetEquippedOffhand().Id == "wood",
           "offhand block overwrites item");

    nlohmann::json serialized;
    ohInv.SerializeToJson(serialized);
    Expect(serialized.contains("equipped_offhand"), "JSON has equipped_offhand");
    Expect(serialized["equipped_offhand"].value("kind", "") == "block" &&
               serialized["equipped_offhand"].value("id", "") == "wood",
           "equipped_offhand JSON fields");

    UCreatureInventory restored;
    restored.DeserializeFromJson(serialized);
    const InventoryEntryRef &roundTrip = restored.GetEquippedOffhand();
    Expect(!roundTrip.empty && roundTrip.kind == InventoryEntryKind::Block &&
               roundTrip.Id == "wood" && roundTrip.count == -1,
           "offhand JSON round-trip");

    restored.UnequipOffhand();
    Expect(restored.GetEquippedOffhand().empty, "UnequipOffhand clears slot");

    InventoryEntryRef bad;
    bad.empty = false;
    bad.kind = InventoryEntryKind::Object;
    bad.Id = "chest";
    Expect(!ohInv.EquipOffhand(bad), "EquipOffhand rejects Object kind");
  }

  // 4) Shield block schema + offhand armor_groups aggregate.
  {
    const fs::path shieldJson = itemsDir / "wood_shield_test.json";
    {
      std::ofstream out(shieldJson);
      out << R"({
  "id": "wood_shield_test",
  "displayName": "Test Shield",
  "wear_end": "destroy",
  "armor": { "armor_groups": { "fleshy": 15 } },
  "block": {
    "enabled": true,
    "damage_mul": 0.25,
    "passive_damage_mul": 0.85,
    "angle_deg": 120,
    "block_uses": 50
  },
  "visual": { "wield_scale": 1.5, "use": { "block": "raise_shield" } },
  "tool": { "punch_attack_uses": 80 }
})";
    }
    const fs::path bowJson = itemsDir / "bow_test.json";
    {
      std::ofstream out(bowJson);
      out << R"({
  "id": "bow_test",
  "displayName": "Test Bow",
  "ranged": {
    "enabled": true,
    "range": 16,
    "ammo_id": "arrow",
    "require_los": true
  },
  "visual": { "wield_scale": 1.4, "use": { "ranged": "draw_bow" } }
})";
    }
    storage.Load(itemsDir.string());
    const ItemDefinition *shield = storage.Get("wood_shield_test");
    Expect(shield != nullptr, "shield definition present");
    if (shield)
    {
      Expect(shield->Block.Enabled, "block.enabled");
      Expect(std::fabs(shield->Block.DamageMul - 0.25f) < 1e-4f, "block.damage_mul");
      Expect(std::fabs(shield->Block.PassiveDamageMul - 0.85f) < 1e-4f,
             "block.passive_damage_mul");
      Expect(shield->Block.BlockUses == 50, "block.block_uses");
      Expect(std::fabs(shield->Visual.WieldScale - 1.5f) < 1e-4f,
             "visual.wield_scale");
      Expect(shield->Visual.Use.Block == "raise_shield", "visual.use.block");
    }
    const ItemDefinition *bow = storage.Get("bow_test");
    Expect(bow != nullptr, "bow definition present");
    if (bow)
    {
      Expect(bow->Ranged.Enabled, "ranged.enabled");
      Expect(std::fabs(bow->Ranged.RangeBlocks - 16.f) < 1e-4f, "ranged.range");
      Expect(bow->Ranged.AmmoId == "arrow", "ranged.ammo_id");
      Expect(bow->Ranged.RequireLos, "ranged.require_los");
    }

    UCreatureInventory shieldInv;
    InventoryEntryRef shieldEntry;
    shieldEntry.empty = false;
    shieldEntry.kind = InventoryEntryKind::Item;
    shieldEntry.Id = "wood_shield_test";
    shieldEntry.count = 1;
    Expect(shieldInv.EquipOffhand(shieldEntry, storage),
           "EquipOffhand shield with items");
    const auto &ohGroups = shieldInv.GetOffhandArmorGroups();
    const auto it = ohGroups.Ratings.find("fleshy");
    Expect(it != ohGroups.Ratings.end() && it->second == 15,
           "offhand armor_groups fleshy from shield");
  }

  fs::remove_all(tmp);

  if (Failures != 0)
  {
    std::cerr << Failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "armor_equipment_test OK" << std::endl;
  return 0;
}

