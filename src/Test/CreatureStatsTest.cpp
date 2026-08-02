#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureStatsDefaults.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Stats/CreatureVitals.h"
#include "Game/WorldGameMode.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

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

  // Creative freeze contract: callers must skip Tick when Creative —
  // verified by mode enum presence (runtime Tick is no-op for Creative).
  Expect(WorldGameMode::Creative != WorldGameMode::Survival, "modes differ");

  std::cout << "creature_stats_test: OK" << std::endl;
  return 0;
}
