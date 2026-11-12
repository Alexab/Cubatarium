#include "Creatures/Core/CreatureInventory.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Gui/Layout/GuiLayout.h"

#include <iostream>
#include <vector>

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
  using cutum::ModePolicy;
  using cutum::UCreatureInventory;
  using cutum::WorldGameMode;

  UCreatureInventory creativeInv;
  if (ModePolicy::ShouldInitCreativeDefaults(WorldGameMode::Creative))
  {
    creativeInv.InitCreativeDefaults();
  }
  Expect(creativeInv.GetStorage().count("wood") > 0,
         "creative init defaults adds wood");
  const auto woodIt = creativeInv.GetStorage().find("wood");
  Expect(woodIt != creativeInv.GetStorage().end() && woodIt->second == -1,
         "creative wood is unlimited");

  UCreatureInventory survivalInv;
  if (ModePolicy::ShouldInitCreativeDefaults(WorldGameMode::Survival))
  {
    survivalInv.InitCreativeDefaults();
  }
  Expect(survivalInv.GetStorage().empty(),
         "survival does not init creative defaults");
  survivalInv.EnsureDefaultHotbar();
  Expect(survivalInv.GetActiveEntryRef() == nullptr,
         "survival default hotbar has empty hands");
  for (const auto &slot : survivalInv.GetHotbar(0).slots)
  {
    Expect(slot.empty || slot.entry.Id.empty(),
           "survival default hotbar has no wood stub");
  }

  UCreatureInventory creativeHotbar;
  creativeHotbar.InitCreativeDefaults();
  creativeHotbar.EnsureDefaultHotbar();
  Expect(creativeHotbar.GetActiveEntryRef() != nullptr &&
             creativeHotbar.GetActiveEntryRef()->Id == "wood",
         "creative default hotbar equips wood");

  for (const auto &entry : survivalInv.GetStorage())
  {
    Expect(entry.second != -1, "survival storage has no unlimited counts");
  }

  UCreatureInventory survivalAfterMigrate;
  survivalAfterMigrate.InitCreativeDefaults();
  survivalAfterMigrate.MigrateCreativeStorageToSurvival();
  Expect(survivalAfterMigrate.GetStorage().empty(),
         "migrate removes unlimited storage entries");
  for (const auto &entry : survivalAfterMigrate.GetStorage())
  {
    Expect(entry.second != -1, "migrate leaves no unlimited counts");
  }

  {
    const std::vector<bool> enabled{false, false, true, false, true};
    Expect(cutum::NearestEnabledIndex(0, enabled) == 2,
           "empty tab falls to the nearest enabled, not the last");
    Expect(cutum::NearestEnabledIndex(3, enabled) == 2,
           "index 3 is closer to 2 than to 4");
    Expect(cutum::NearestEnabledIndex(4, enabled) == 4,
           "an enabled tab stays selected");
    Expect(cutum::StackIsUnlimited(-1), "negative count is unlimited");
    Expect(!cutum::StackIsUnlimited(1), "finite stack can be consumed");
  }

  if (Failures != 0)
  {
    std::cerr << "inventory_mode_test: " << Failures << " failure(s)"
              << std::endl;
    return 1;
  }

  std::cout << "inventory_mode_test: OK" << std::endl;
  return 0;
}
