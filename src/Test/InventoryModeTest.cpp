#include "Creatures/Core/CreatureInventory.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"

#include <iostream>

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

  if (Failures != 0)
  {
    std::cerr << "inventory_mode_test: " << Failures << " failure(s)"
              << std::endl;
    return 1;
  }

  std::cout << "inventory_mode_test: OK" << std::endl;
  return 0;
}
