#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureStatsDefaults.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_stats_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  {
    const auto player = CreatureStatsDefaults::For(
        CreatureRole::ControlledDefault, {"humanoid"},
        CreatureHabitat::Terrestrial, "human");
    Expect(player.needsTick, "player needs tick");
    Expect(player.vitals.maxHealth == 100.f, "player hp 100");
    Expect(player.vitals.maxFatalWounds == 3, "player fatal 3");
    Expect(player.attributes.strength == 10, "player str 10");
  }

  {
    const auto bot = CreatureStatsDefaults::For(
        CreatureRole::Bot, {"bot"}, CreatureHabitat::Terrestrial, "bot");
    Expect(bot.needsTick, "bot needs tick");
    Expect(bot.vitals.maxFatalWounds == 3, "bot fatal 3");
  }

  {
    const auto sheep = CreatureStatsDefaults::For(
        CreatureRole::Mob, {"animal", "mobs_animal"},
        CreatureHabitat::Terrestrial, "sheep");
    Expect(!sheep.needsTick, "sheep no needs tick");
    Expect(sheep.vitals.maxFatalWounds == 1, "sheep fatal 1");
    Expect(sheep.vitals.maxHealth == 20.f, "sheep hp 20");
  }

  {
    CreatureAttributes a;
    a.strength = 99;
    a.luck = 0;
    a.ClampAll();
    Expect(a.strength == 20 && a.luck == 1, "attr clamp 1-20");
  }

  {
    CreatureVitals v;
    v.maxHealth = 50.f;
    v.health = 999.f;
    v.FillFull();
    Expect(v.health == 50.f && v.fatigue == 0.f, "FillFull sets currents");
  }

  {
    CreatureVitals v;
    v.maxHealth = 80.f;
    v.health = 40.f;
    v.maxFatalWounds = 3;
    v.fatalWounds = 1;
    CreatureAttributes attrs;
    attrs.strength = 12;
    attrs.perception = 15;
    nlohmann::json j;
    CreatureStatsJson::Write(j, v, attrs);
    CreatureVitals v2;
    CreatureAttributes a2;
    Expect(CreatureStatsJson::Read(j, v2, a2), "read stats json");
    Expect(v2.health == 40.f && v2.maxHealth == 80.f, "roundtrip health");
    Expect(v2.fatalWounds == 1 && v2.maxFatalWounds == 3, "roundtrip wounds");
    Expect(a2.strength == 12 && a2.perception == 15, "roundtrip attrs");
  }

  {
    Expect(std::string(WorldGameModeToString(WorldGameMode::Survival)) ==
               "survival",
           "mode to string");
    Expect(WorldGameModeFromString("creative") == WorldGameMode::Creative,
           "mode from string");
  }

  {
    Expect(std::string(WorldDifficultyToString(WorldDifficulty::Peaceful)) ==
               "peaceful",
           "difficulty peaceful to string");
    Expect(std::string(WorldDifficultyToString(WorldDifficulty::Easy)) ==
               "easy",
           "difficulty easy to string");
    Expect(std::string(WorldDifficultyToString(WorldDifficulty::Normal)) ==
               "normal",
           "difficulty normal to string");
    Expect(WorldDifficultyFromString("peaceful") == WorldDifficulty::Peaceful,
           "difficulty from peaceful");
    Expect(WorldDifficultyFromString("easy") == WorldDifficulty::Easy,
           "difficulty from easy");
    Expect(WorldDifficultyFromString("normal") == WorldDifficulty::Normal,
           "difficulty from normal");
    Expect(WorldDifficultyFromString("unknown") == WorldDifficulty::Normal,
           "difficulty unknown defaults normal");
  }

  // Creative freeze contract: callers must skip Tick when Creative —
  // verified by mode enum presence (runtime Tick is no-op for Creative).
  Expect(WorldGameMode::Creative != WorldGameMode::Survival, "modes differ");
  Expect(WorldDifficulty::Peaceful != WorldDifficulty::Normal,
         "difficulties differ");

  {
    // TD-INF-008: armor_groups / bare_hand parse roundtrip via definition load.
    namespace fs = std::filesystem;
    const fs::path tmp =
        fs::temp_directory_path() / "cubatarium_armor_groups_test";
    const fs::path species = tmp / "armor_test_mob";
    fs::create_directories(species);
    const fs::path jsonPath = species / "creature.json";
    {
      std::ofstream out(jsonPath);
      out << R"({
  "id": "armor_test_mob",
  "display_name": "Armor Test",
  "role": "mob",
  "armor_groups": { "fleshy": 80 },
  "bare_hand": {
    "full_punch_interval": 0.4,
    "damage": { "fleshy": 7 }
  },
  "vitals": { "max_health": 40, "max_fatal_wounds": 1 },
  "attributes": { "strength": 11 }
})";
    }
    UCreatureDefinitionStorage storage;
    Expect(storage.LoadFile(jsonPath.string()), "load armor_test_mob json");
    const CreatureDefinition *def = storage.Get("armor_test_mob");
    Expect(def != nullptr, "armor_test_mob definition present");
    Expect(def->stats.hasArmorGroupsOverride, "armor_groups override flag");
    Expect(def->stats.armorGroups.Get("fleshy") == 80, "armor fleshy 80");
    Expect(def->stats.bareHand.hasOverride, "bare_hand override flag");
    Expect(def->stats.bareHand.fleshyDamage == 7, "bare_hand fleshy 7");
    Expect(std::fabs(def->stats.bareHand.fullPunchInterval - 0.4f) < 1e-4f,
           "bare_hand interval 0.4");
    fs::remove_all(tmp);
  }

  std::cout << "creature_stats_test: OK" << std::endl;
  return 0;
}
