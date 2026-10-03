#include "Items/ToolCapabilities.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemVisualDefaults.h"
#include "Blocks/BlockDefinition.h"
#include "Creatures/Influence/InfluenceTypes.h"
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

  ToolCapabilitiesDef hit_tool;
  hit_tool.FullPunchInterval = 1.0f;
  hit_tool.PunchAttackUses = 100;
  hit_tool.Damage.Groups["fleshy"] = 8;
  ArmorGroups armor = ArmorGroups::DefaultFleshy();
  attrs.accuracy = 10;
  HitParams hit = ResolveHitParams(armor, hit_tool, attrs, 1.0f);
  Expect(hit.DidHit && !hit.Missed, "hit lands");
  Expect(std::fabs(hit.Damage - 8.f) < 0.05f, "hit damage 8");
  Expect(hit.WearDelta > 0.f, "punch wear delta");

  attrs.accuracy = 1;
  HitParams maybe_miss = ResolveHitParams(armor, hit_tool, attrs, 0.01f);
  Expect(maybe_miss.Missed || maybe_miss.DidHit, "low accuracy resolves");

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

  // Category / explicit wield_scale defaults (ItemVisualDefaults).
  {
    ItemDefinition spear;
    spear.Id = "wood_spear";
    Expect(std::fabs(DefaultWieldScale(spear) - 1.80f) < 1e-4f,
           "spear category scale");
    ItemDefinition food;
    food.Id = "apple";
    food.Use.Action = ItemUseActionKind::Eat;
    Expect(std::fabs(DefaultWieldScale(food) - 0.95f) < 1e-4f, "eat scale");
    ItemDefinition bow;
    bow.Id = "wood_bow";
    bow.Ranged.Enabled = true;
    Expect(std::fabs(DefaultWieldScale(bow) - 1.40f) < 1e-4f, "bow scale");
    ItemDefinition scaled = spear;
    scaled.Visual.HasWieldScale = true;
    scaled.Visual.WieldScale = 2.5f;
    Expect(std::fabs(DefaultWieldScale(scaled) - 2.5f) < 1e-4f,
           "explicit wield_scale wins");
    Expect(DefaultSwingPreset(spear, FpSwingKind::Melee) == "thrust_spear",
           "spear melee preset");
  }

  if (Failures != 0)
  {
    std::cerr << Failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "tool_capabilities_test OK" << std::endl;
  return 0;
}
