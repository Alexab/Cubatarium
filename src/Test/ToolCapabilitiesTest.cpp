#include "Items/ToolCapabilities.h"
#include "Items/ItemDefinition.h"
#include "Blocks/BlockDefinition.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include <cassert>
#include <cmath>
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
  Expect(!IsToolWearEnabled(WorldGameMode::Creative, WorldDifficulty::Normal),
         "creative no wear");
  Expect(!IsToolWearEnabled(WorldGameMode::Survival, WorldDifficulty::Peaceful),
         "peaceful no wear");
  Expect(IsToolWearEnabled(WorldGameMode::Survival, WorldDifficulty::Normal),
         "normal survival wears");

  ItemDefinition pick;
  pick.Id = "stone_pickaxe";
  ToolGroupCap cap;
  cap.MaxLevel = 1;
  cap.Uses = 60;
  cap.Times[3] = 0.7f;
  cap.Times[2] = 1.0f;
  pick.Tool.GroupCaps["cracky"] = cap;

  BlockDefinition stone;
  stone.Name = "stone";
  stone.Hardness = 1.5f;
  stone.DigGroups["cracky"] = 3;
  stone.DigLevel = 0;

  CreatureAttributes attrs;
  attrs.strength = 10;
  DigParams dig =
      ResolveDigParams(&pick, stone, attrs, WorldGameMode::Survival);
  Expect(dig.Effective, "pick digs stone");
  Expect(dig.DurationSec > 0.f, "positive dig time");
  Expect(dig.WearDelta > 0.f, "wear on dig");
  Expect(dig.MainGroup == "cracky", "main group cracky");

  DigParams creative =
      ResolveDigParams(&pick, stone, attrs, WorldGameMode::Creative);
  Expect(creative.DurationSec == 0.f, "creative instant");
  Expect(creative.WearDelta == 0.f, "creative no wear delta");

  InventoryEntryRef entry;
  entry.empty = false;
  entry.kind = InventoryEntryKind::Item;
  entry.Id = "stone_pickaxe";
  pick.WearEnd = ItemWearEnd::Destroy;
  Expect(!ApplyItemWear(entry, pick, 0.5f, false), "wear disabled");
  Expect(entry.wear == 0.f, "wear unchanged when disabled");
  Expect(ApplyItemWear(entry, pick, 1.f, true), "destroy clears");
  Expect(entry.empty, "destroyed entry empty");

  if (Failures != 0)
  {
    std::cerr << Failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "tool_capabilities_test OK" << std::endl;
  return 0;
}
