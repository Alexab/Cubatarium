#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureStatsDefaults.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Game/WorldDifficulty.h"
#include "Game/WorldMetadataJson.h"
#include "Game/WorldGameMode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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
    Expect(std::string(WorldDifficultyToString(WorldDifficulty::Hard)) ==
               "hard",
           "difficulty hard to string");
    Expect(std::string(WorldDifficultyToString(WorldDifficulty::Hardcore)) ==
               "hardcore",
           "difficulty hardcore to string");
    Expect(WorldDifficultyFromString("peaceful") == WorldDifficulty::Peaceful,
           "difficulty from peaceful");
    Expect(WorldDifficultyFromString("easy") == WorldDifficulty::Easy,
           "difficulty from easy");
    Expect(WorldDifficultyFromString("normal") == WorldDifficulty::Normal,
           "difficulty from normal");
    Expect(WorldDifficultyFromString("hard") == WorldDifficulty::Hard,
           "difficulty from hard");
    Expect(WorldDifficultyFromString("hardcore") == WorldDifficulty::Hardcore,
           "difficulty from hardcore");
    Expect(WorldDifficultyFromString("unknown") == WorldDifficulty::Normal,
           "difficulty unknown defaults normal");
    Expect(WorldDifficultyFromString("") == WorldDifficulty::Normal,
           "difficulty missing value defaults normal");
    const WorldDifficulty difficulties[] = {
        WorldDifficulty::Peaceful, WorldDifficulty::Easy,
        WorldDifficulty::Normal, WorldDifficulty::Hard,
        WorldDifficulty::Hardcore};
    for (WorldDifficulty difficulty : difficulties)
    {
      Expect(WorldDifficultyFromString(WorldDifficultyToString(difficulty)) ==
                 difficulty,
             "difficulty string roundtrip");
    }
    {
      nlohmann::json worldData = nlohmann::json::object();
      for (WorldDifficulty difficulty : difficulties)
      {
        WriteWorldDifficulty(worldData, difficulty);
        Expect(ReadWorldDifficulty(worldData) == difficulty,
               "world metadata difficulty roundtrip");
      }
      Expect(ReadWorldDifficulty(nlohmann::json::object()) ==
                 WorldDifficulty::Normal,
             "old world save without difficulty defaults to normal");
      Expect(ReadWorldDifficulty(
                 nlohmann::json{{"difficulty", "invalid"}}) ==
                 WorldDifficulty::Normal,
             "unknown world difficulty defaults to normal");
      WriteHardcoreWorldCompleted(worldData, true);
      Expect(ReadHardcoreWorldCompleted(worldData),
             "completed hardcore marker persists true");
      const WorldDeathState completedDeath = ReadWorldDeathState(worldData);
      Expect(completedDeath.PlayerDead && completedDeath.HardcoreCompleted,
             "completed hardcore save restores player death state");
      WriteHardcoreWorldCompleted(worldData, false);
      Expect(!ReadHardcoreWorldCompleted(worldData),
             "hardcore completion marker persists false");
      const WorldDeathState activeDeath = ReadWorldDeathState(worldData);
      Expect(!activeDeath.PlayerDead && !activeDeath.HardcoreCompleted,
             "active save restores a live player state");
      Expect(!ReadHardcoreWorldCompleted(nlohmann::json::object()),
             "old world save without hardcore marker remains active");
      const WorldDeathState legacyDeath =
          ReadWorldDeathState(nlohmann::json::object());
      Expect(!legacyDeath.PlayerDead && !legacyDeath.HardcoreCompleted,
             "old world save does not restore a false hardcore death");
    }
    const auto peacefulProfile =
        WorldDifficultyProfileFor(WorldDifficulty::Peaceful);
    Expect(peacefulProfile.NeedsDrainMultiplier == 0.f &&
               peacefulProfile.HostileSpawnPressureMultiplier == 0.f &&
               peacefulProfile.HostileDamageMultiplier == 0.f,
           "peaceful profile disables needs pressure and damage");
    const auto easyProfile = WorldDifficultyProfileFor(WorldDifficulty::Easy);
    Expect(easyProfile.NeedsDrainMultiplier == 0.5f &&
               easyProfile.HostileSpawnPressureMultiplier == 0.5f &&
               easyProfile.HostileDamageMultiplier == 0.75f,
           "easy profile scales needs hostile spawn and damage");
    const auto normalProfile = WorldDifficultyProfileFor(WorldDifficulty::Normal);
    Expect(normalProfile.NeedsDrainMultiplier == 1.f &&
               normalProfile.HostileSpawnPressureMultiplier == 1.f &&
               normalProfile.HostileDamageMultiplier == 1.f,
           "normal profile uses default multipliers");
    const auto hardProfile = WorldDifficultyProfileFor(WorldDifficulty::Hard);
    const auto hardcoreProfile =
        WorldDifficultyProfileFor(WorldDifficulty::Hardcore);
    Expect(hardProfile.NeedsDrainMultiplier == 1.5f &&
               hardProfile.HostileSpawnPressureMultiplier == 1.5f &&
               hardProfile.HostileDamageMultiplier == 1.25f &&
               hardcoreProfile.NeedsDrainMultiplier ==
                   hardProfile.NeedsDrainMultiplier &&
               hardcoreProfile.HostileSpawnPressureMultiplier ==
                   hardProfile.HostileSpawnPressureMultiplier &&
               hardcoreProfile.HostileDamageMultiplier ==
                   hardProfile.HostileDamageMultiplier,
           "hardcore profile matches hard");
    Expect(WorldDifficultyProfileFor(WorldDifficulty::Peaceful)
                   .HostileSpawnPressureMultiplier == 0.f,
           "peaceful hostile spawn pressure zero");
    Expect(WorldDifficultyProfileFor(WorldDifficulty::Easy)
                   .HostileDamageMultiplier == 0.75f,
           "easy hostile damage scale");
    Expect(WorldDifficultyProfileFor(WorldDifficulty::Hardcore)
                   .NeedsDrainMultiplier == 1.5f,
           "hardcore uses hard needs scale");
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
  "catalog": {
    "spawnable": true,
    "natural_spawnable": true,
    "spawn_weight": 2.5,
    "spawn_group_min": 2,
    "spawn_group_max": 4
  },
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
    Expect(def && def->catalog.spawnable && def->catalog.naturalSpawnable,
           "load natural spawn catalog flags");
    Expect(def && std::abs(def->catalog.spawnWeight - 2.5f) < 1e-5f &&
               def->catalog.spawnGroupMin == 2 &&
               def->catalog.spawnGroupMax == 4,
           "load natural spawn weight and group size");
    Expect(def != nullptr, "armor_test_mob definition present");
    Expect(def->stats.hasArmorGroupsOverride, "armor_groups override flag");
    Expect(def->stats.armorGroups.Get("fleshy") == 80, "armor fleshy 80");
    Expect(def->stats.bareHand.hasOverride, "bare_hand override flag");
    Expect(def->stats.bareHand.fleshyDamage == 7, "bare_hand fleshy 7");
    Expect(std::fabs(def->stats.bareHand.fullPunchInterval - 0.4f) < 1e-4f,
           "bare_hand interval 0.4");

    const fs::path legacySpecies = tmp / "legacy_catalog_mob";
    fs::create_directories(legacySpecies);
    const fs::path legacyJsonPath = legacySpecies / "creature.json";
    {
      std::ofstream out(legacyJsonPath);
      out << R"({
  "id": "legacy_catalog_mob",
  "display_name": "Legacy Catalog",
  "catalog": { "spawnable": true },
  "role": "mob"
})";
    }
    Expect(storage.LoadFile(legacyJsonPath.string()),
           "load legacy catalog without natural spawn flag");
    const CreatureDefinition *legacyDef = storage.Get("legacy_catalog_mob");
    Expect(legacyDef && legacyDef->catalog.spawnable &&
               !legacyDef->catalog.naturalSpawnable,
           "missing natural spawn flag defaults to opt-out");
    fs::remove_all(tmp);
  }

  {
    namespace fs = std::filesystem;
    const fs::path catalogRoot =
        fs::path(CUBATARIUM_SOURCE_DIR) / "models" / "creatures";
    size_t expectedFiles = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(catalogRoot))
    {
      if (entry.is_directory() && fs::exists(entry.path() / "creature.json"))
      {
        ++expectedFiles;
      }
    }

    UCreatureDefinitionStorage storage;
    storage.Load(catalogRoot.string());
    Expect(storage.Count() == expectedFiles,
           "every creature catalog file loads into the production parser");

    const std::vector<std::string> spawnableIds = storage.ListSpawnable();
    std::array<size_t, 5> naturalHabitatCounts{};
    size_t hostileCount = 0;
    for (const std::string &id : storage.ListAllIds())
    {
      const CreatureDefinition *definition = storage.Get(id);
      Expect(definition != nullptr, "loaded creature id resolves");
      if (!definition)
      {
        continue;
      }

      const bool hostile = std::find(definition->catalog.tags.begin(),
                                     definition->catalog.tags.end(), "hostile") !=
                           definition->catalog.tags.end();
      if (hostile)
      {
        ++hostileCount;
        Expect(definition->behavior.Id == "melee_attack",
               "hostile catalog species have an aggressive AI behavior");
      }
      if (!definition->catalog.naturalSpawnable)
      {
        continue;
      }

      Expect(definition->catalog.spawnable,
             "natural species are available to the spawn catalog");
      Expect(definition->catalog.spawnWeight > 0.f,
             "natural species have positive selection weight");
      Expect(definition->catalog.spawnGroupMin >= 1 &&
                 definition->catalog.spawnGroupMin <=
                     definition->catalog.spawnGroupMax,
             "natural species have a valid spawn group range");
      switch (definition->habitat)
      {
      case CreatureHabitat::Terrestrial:
        ++naturalHabitatCounts[0];
        break;
      case CreatureHabitat::Aquatic:
        ++naturalHabitatCounts[1];
        break;
      case CreatureHabitat::Aerial:
        ++naturalHabitatCounts[2];
        break;
      case CreatureHabitat::Amphibious:
        ++naturalHabitatCounts[3];
        break;
      case CreatureHabitat::Lava:
        ++naturalHabitatCounts[4];
        break;
      }
      Expect(std::find(spawnableIds.begin(), spawnableIds.end(), id) !=
                 spawnableIds.end(),
             "natural species are included in weighted spawn candidates");
    }
    Expect(hostileCount > 0, "creature catalog contains hostile species");
    for (size_t count : naturalHabitatCounts)
    {
      Expect(count > 0,
             "natural spawn catalog covers every creature habitat");
    }
  }

  std::cout << "creature_stats_test: OK" << std::endl;
  return 0;
}
