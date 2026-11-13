#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemUseRegistry.h"

#include <filesystem>
#include <fstream>
#include <iostream>

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

  // Drink use params for waterskin-style items.
  {
    ItemDefinition skin;
    skin.Id = "waterskin";
    skin.Use.Action = ItemUseActionKind::Drink;
    skin.Use.Thirst = 40.f;
    const ItemUseParams use = ItemUseRegistry::FromDefinition(skin);
    Expect(use.Action == ItemUseAction::Drink, "waterskin drink action");
    Expect(use.ThirstDelta == 40.f, "waterskin thirst");
    Expect(use.SatietyDelta == 0.f, "waterskin no satiety");
    Expect(!ItemUseConsumes(ItemUseAction::Drink), "drink does not consume");
    Expect(ItemUseConsumes(ItemUseAction::Eat), "eat consumes");
  }

  // Creative grants no item drops; Survival does.
  {
    UCreatureInventory creative;
    creative.InitCreativeDefaults();
    ResourceEconomyService::GrantItemDrop(WorldGameMode::Creative, creative,
                                          "raw_meat", 1);
    Expect(creative.GetStorage().count("raw_meat") == 0,
           "creative skips meat drop");

    UCreatureInventory survival;
    ResourceEconomyService::GrantItemDrop(WorldGameMode::Survival, survival,
                                          "raw_meat", 1);
    ResourceEconomyService::GrantItemDrop(WorldGameMode::Survival, survival,
                                          "leather", 1);
    Expect(survival.GetStorage().at("raw_meat") == 1, "survival meat grant");
    Expect(survival.GetStorage().at("leather") == 1, "survival leather grant");
  }

  // Creature drops field parse (cow-like).
  {
    const fs::path tmp =
        fs::temp_directory_path() / "cubatarium_item_survival_hooks";
    fs::create_directories(tmp);
    const fs::path cow = tmp / "cow.json";
    {
      std::ofstream out(cow);
      out << R"({
  "id": "cow_drop_probe",
  "display_name": "Cow",
  "role": "mob",
  "bounds": {"rest":[1,1,1],"max":[1,1,1],"min":[1,1,1]},
  "drops": [{"id":"raw_meat","count":1},{"id":"leather","count":1}]
})";
    }
    UCreatureDefinitionStorage storage;
    Expect(storage.LoadFile(cow.string()), "load cow_drop_probe");
    const CreatureDefinition *def = storage.Get("cow_drop_probe");
    Expect(def != nullptr, "def present");
    Expect(def && def->drops.size() == 2, "two drops");
    Expect(def && def->drops[0].itemId == "raw_meat" &&
               def->drops[0].count == 1,
           "meat drop");
    Expect(def && def->drops[1].itemId == "leather" && def->drops[1].count == 1,
           "leather drop");

    // Simulate killer grant from parsed drops (same path InfluenceApplier
    // uses).
    UCreatureInventory killer;
    for (const CreatureDropEntry &drop : def->drops)
    {
      ResourceEconomyService::GrantItemDrop(WorldGameMode::Survival, killer,
                                            drop.itemId, drop.count);
    }
    Expect(killer.GetStorage().at("raw_meat") == 1, "killer received meat");
    Expect(killer.GetStorage().at("leather") == 1, "killer received leather");

    // Unequipped RemoveCreature analogue: no automatic grant without killer
    // loop.
    UCreatureInventory empty;
    Expect(empty.GetStorage().empty(), "no grant without killer path");
  }

  // Legacy 6-slot armor migrates arms/hands into storage.
  {
    UCreatureInventory inv;
    nlohmann::json data = nlohmann::json::object();
    data["equipped_armor"] = nlohmann::json::array(
        {nlohmann::json{{"id", "leather_head"}, {"count", 1}, {"empty", false}},
         nlohmann::json{
             {"id", "leather_chest"}, {"count", 1}, {"empty", false}},
         nlohmann::json{{"id", "legacy_arms"}, {"count", 1}, {"empty", false}},
         nlohmann::json{{"id", "legacy_hands"}, {"count", 1}, {"empty", false}},
         nlohmann::json{{"id", "leather_legs"}, {"count", 1}, {"empty", false}},
         nlohmann::json{
             {"id", "leather_feet"}, {"count", 1}, {"empty", false}}});
    inv.DeserializeFromJson(data);
    Expect(inv.GetEquippedArmor(0).Id == "leather_head", "migrated head");
    Expect(inv.GetEquippedArmor(1).Id == "leather_chest", "migrated chest");
    Expect(inv.GetEquippedArmor(2).Id == "leather_legs", "migrated legs");
    Expect(inv.GetEquippedArmor(3).Id == "leather_feet", "migrated feet");
    Expect(inv.GetRecovery().size() == 2 &&
               inv.GetRecovery()[0].Id == "legacy_arms",
           "arms recovered");
    Expect(inv.GetRecovery().size() == 2 &&
               inv.GetRecovery()[1].Id == "legacy_hands",
           "hands recovered");
  }

  if (Failures != 0)
  {
    std::cerr << Failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "item_survival_hooks_test OK" << std::endl;
  return 0;
}
