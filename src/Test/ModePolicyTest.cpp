#include "Game/ModePolicy.h"
#include "Creatures/Core/CreaturePersistenceJson.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Spawning/CreatureLoadRecovery.h"
#include "Creatures/Spawning/CreaturePopulationRules.h"

#include <iostream>
#include <nlohmann/json.hpp>

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
  using cutum::CreatureHabitat;
  using cutum::CreaturePopulationCounts;
  using cutum::CreaturePopulationRules;
  using cutum::ModePolicy;
  using cutum::EnvironmentSample;
  using cutum::HabitatMatches;
  using cutum::CreatureSpawnSource;
  using cutum::CountsTowardNaturalPopulationLimit;
  using cutum::CreatureSpawnSourceFromString;
  using cutum::CreatureSpawnSourceToString;
  using cutum::WorldDifficulty;
  using cutum::WorldGameMode;

  const auto creative = WorldGameMode::Creative;
  const auto survival = WorldGameMode::Survival;

  Expect(ModePolicy::AllowsNeedsTick(survival), "survival needs tick");
  Expect(!ModePolicy::AllowsNeedsTick(creative), "creative no needs tick");

  Expect(ModePolicy::AllowsCombatDamage(survival), "survival combat");
  Expect(!ModePolicy::AllowsCombatDamage(creative), "creative no combat");

  Expect(ModePolicy::AllowsDigProgress(creative, 0.f), "creative dig hardness 0");
  Expect(!ModePolicy::AllowsDigProgress(survival, 0.f), "survival dig hardness 0");
  Expect(ModePolicy::AllowsDigProgress(survival, 1.f), "survival dig hardness 1");

  Expect(ModePolicy::IsCreativeInstantDig(creative), "creative instant dig");
  Expect(!ModePolicy::IsCreativeInstantDig(survival), "survival not instant dig");

  Expect(!ModePolicy::AllowsToolWear(creative, WorldDifficulty::Normal),
         "creative no wear");
  Expect(!ModePolicy::AllowsToolWear(survival, WorldDifficulty::Peaceful),
         "peaceful no wear");
  Expect(ModePolicy::AllowsToolWear(survival, WorldDifficulty::Normal),
         "survival normal wear");

  Expect(ModePolicy::AllowsHostileAggro(survival, WorldDifficulty::Normal),
         "survival normal aggro");
  Expect(!ModePolicy::AllowsHostileAggro(creative, WorldDifficulty::Normal),
         "creative no aggro");
  Expect(!ModePolicy::AllowsHostileAggro(survival, WorldDifficulty::Peaceful),
         "peaceful no aggro");
  Expect(!ModePolicy::AllowsHostileSpawning(survival,
                                            WorldDifficulty::Peaceful),
         "peaceful no hostile spawn");
  Expect(ModePolicy::AllowsHostileSpawning(creative, WorldDifficulty::Normal),
         "creative permits harmless hostile wildlife spawns");
  Expect(!ModePolicy::AllowsHostileSpawning(creative,
                                            WorldDifficulty::Peaceful),
         "creative peaceful suppresses hostile spawns");
  Expect(ModePolicy::AllowsHostileSpawning(survival, WorldDifficulty::Hardcore),
         "hardcore hostile spawn");
  Expect(ModePolicy::HostileDamageMultiplier(WorldDifficulty::Hard) == 1.25f,
         "hard hostile damage scale");
  Expect(ModePolicy::HostileDamageMultiplier(WorldDifficulty::Peaceful) == 0.f,
         "peaceful hostile damage disabled");
  Expect(ModePolicy::HostileDamageMultiplier(WorldDifficulty::Easy) == 0.75f,
         "easy hostile damage scale");
  Expect(ModePolicy::HostileDamageMultiplier(WorldDifficulty::Normal) == 1.f,
         "normal hostile damage scale");
  Expect(ModePolicy::HostileDamageMultiplier(WorldDifficulty::Hardcore) ==
             ModePolicy::HostileDamageMultiplier(WorldDifficulty::Hard),
         "hardcore uses hard hostile damage scale");

  Expect(ModePolicy::AllowsCreatureSpawn(
             creative, CreatureSpawnSource::CreativeManual),
         "creative manual spawn");
  Expect(!ModePolicy::AllowsCreatureSpawn(
             survival, CreatureSpawnSource::CreativeManual),
         "survival blocks manual spawn");
  Expect(ModePolicy::AllowsCreatureSpawn(
             creative, CreatureSpawnSource::DebugCommand),
         "creative debug spawn");
  Expect(!ModePolicy::AllowsCreatureSpawn(
             survival, CreatureSpawnSource::DebugCommand),
         "survival blocks debug spawn");
  Expect(ModePolicy::AllowsCreatureSpawn(
             survival, CreatureSpawnSource::NaturalPopulation),
         "survival natural spawn");
  Expect(ModePolicy::AllowsCreatureSpawn(
             creative, CreatureSpawnSource::NaturalPopulation),
         "creative natural spawn");
  Expect(ModePolicy::AllowsCreatureSpawn(creative, CreatureSpawnSource::Qa),
         "creative qa spawn");
  Expect(!ModePolicy::AllowsCreatureSpawn(survival, CreatureSpawnSource::Qa),
         "survival blocks qa spawn");
  Expect(ModePolicy::AllowsCreatureSpawn(
             survival, CreatureSpawnSource::Persistence),
         "persistence restoration allowed");
  Expect(!ModePolicy::AllowsCreatureSpawn(
             survival, CreatureSpawnSource::LegacySave),
         "legacy source not a new spawn request");
  Expect(CreatureSpawnSourceFromString(CreatureSpawnSourceToString(
             CreatureSpawnSource::NaturalPopulation)) ==
             CreatureSpawnSource::NaturalPopulation,
         "spawn source serialization roundtrip");
  Expect(CreatureSpawnSourceFromString("survival_natural") ==
             CreatureSpawnSource::NaturalPopulation,
         "old survival natural source remains readable");
  Expect(CreatureSpawnSourceFromString("missing") ==
             CreatureSpawnSource::LegacySave,
         "unknown old spawn source remains legacy");
  Expect(CountsTowardNaturalPopulationLimit(
             CreatureSpawnSource::NaturalPopulation),
         "natural spawn counts against natural population cap");
  Expect(CountsTowardNaturalPopulationLimit(CreatureSpawnSource::LegacySave),
         "legacy creature counts conservatively against natural population cap");
  Expect(!CountsTowardNaturalPopulationLimit(
             CreatureSpawnSource::CreativeManual),
         "known creative manual spawn does not consume natural population cap");
  {
    const CreatureSpawnSource sources[] = {
        CreatureSpawnSource::CreativeManual,
        CreatureSpawnSource::NaturalPopulation,
        CreatureSpawnSource::DebugCommand,
        CreatureSpawnSource::Qa,
        CreatureSpawnSource::Persistence,
        CreatureSpawnSource::LegacySave};
    for (const CreatureSpawnSource source : sources)
    {
      nlohmann::json entity;
      cutum::WriteCreatureSpawnSource(entity, source);
      Expect(cutum::ReadCreatureSpawnSource(entity) == source,
             "creature spawn source JSON roundtrip");
    }
    Expect(cutum::ReadCreatureSpawnSource(nlohmann::json::object()) ==
               CreatureSpawnSource::LegacySave,
           "missing spawn source in old creature save defaults to legacy");
    Expect(cutum::ReadCreatureSpawnSource(
               nlohmann::json{{"spawn_source", 7}}) ==
               CreatureSpawnSource::LegacySave,
           "invalid spawn source in old creature save defaults to legacy");

    nlohmann::json savedNaturalCreature;
    cutum::WriteCreatureSpawnSource(
        savedNaturalCreature, CreatureSpawnSource::NaturalPopulation);
    const glm::vec3 savedOrigin(13.5f, 27.f, -4.25f);
    cutum::WriteCreatureBodyOrigin(savedNaturalCreature, savedOrigin);
    const CreatureSpawnSource restoredSource =
        cutum::ReadCreatureSpawnSource(savedNaturalCreature);
    const glm::vec3 restoredOrigin = cutum::ReadCreatureBodyOrigin(
        savedNaturalCreature, glm::vec3(0.f));
    Expect(restoredOrigin == savedOrigin,
           "natural creature persistence keeps its saved world position");
    CreaturePopulationCounts restoredPopulation{
        CreaturePopulationRules::NaturalWorldLimit - 1,
        CreaturePopulationRules::LocalLimit - 1,
        CreaturePopulationRules::HostileWorldLimitBase - 1,
        CreaturePopulationRules::HostileLocalLimitBase - 1};
    CreaturePopulationRules::Accumulate(restoredPopulation, restoredSource,
                                        true, true);
    Expect(restoredPopulation.naturalWorld ==
                   CreaturePopulationRules::NaturalWorldLimit &&
               restoredPopulation.local == CreaturePopulationRules::LocalLimit &&
               restoredPopulation.hostileWorld ==
                   CreaturePopulationRules::HostileWorldLimitBase &&
               restoredPopulation.hostileLocal ==
                   CreaturePopulationRules::HostileLocalLimitBase,
           "restored natural creature contributes to population counts");
    Expect(!CreaturePopulationRules::CanAddNaturalCreature(
               restoredPopulation, false, WorldDifficulty::Normal),
           "restored natural creature prevents exceeding population caps");
    Expect(!CreaturePopulationRules::CanAddNaturalCreature(
               restoredPopulation, true, WorldDifficulty::Normal),
           "restored hostile creature prevents exceeding hostile caps");

    const WorldGameMode modes[] = {creative, survival};
    for (const WorldGameMode mode : modes)
    {
      for (const CreatureSpawnSource source : sources)
      {
        const bool expected =
            source == CreatureSpawnSource::Persistence ||
            (mode == creative &&
             (source == CreatureSpawnSource::CreativeManual ||
              source == CreatureSpawnSource::DebugCommand ||
              source == CreatureSpawnSource::Qa)) ||
            source == CreatureSpawnSource::NaturalPopulation;
        Expect(ModePolicy::AllowsCreatureSpawn(mode, source) == expected,
               "spawn policy covers every source and game mode");
      }
    }
  }
  Expect(ModePolicy::AllowsDifficultyCommand(WorldDifficulty::Hard,
                                             WorldDifficulty::Normal),
         "hard difficulty can be changed");
  Expect(!ModePolicy::AllowsDifficultyCommand(WorldDifficulty::Hardcore,
                                             WorldDifficulty::Normal),
         "hardcore difficulty is locked");
  Expect(ModePolicy::AllowsDifficultyCommand(WorldDifficulty::Hardcore,
                                             WorldDifficulty::Hardcore),
         "hardcore session can sync its current difficulty");
  Expect(!ModePolicy::AllowsDifficultyCommand(WorldDifficulty::Hard,
                                             WorldDifficulty::Hardcore),
         "hardcore can only be selected at world creation");
  const WorldDifficulty difficultyValues[] = {
      WorldDifficulty::Peaceful, WorldDifficulty::Easy,
      WorldDifficulty::Normal, WorldDifficulty::Hard,
      WorldDifficulty::Hardcore};
  for (const WorldDifficulty current : difficultyValues)
  {
    for (const WorldDifficulty requested : difficultyValues)
    {
      const bool expected =
          current == requested ||
          (current != WorldDifficulty::Hardcore &&
           requested != WorldDifficulty::Hardcore);
      Expect(ModePolicy::AllowsDifficultyCommand(current, requested) ==
                 expected,
             "difficulty command policy covers all difficulty pairs");
    }
  }
  Expect(!ModePolicy::AllowsPlayerRespawn(WorldDifficulty::Hardcore),
         "hardcore player cannot respawn");

  {
    EnvironmentSample aerialInWater;
    aerialInWater.inOpenAir = true;
    aerialInWater.inFluid = true;
    aerialInWater.inWater = true;
    Expect(!HabitatMatches(CreatureHabitat::Aerial, aerialInWater),
           "aerial habitat rejects liquid even when surrounded by open air");
    Expect(HabitatMatches(CreatureHabitat::Aquatic, aerialInWater),
           "aquatic habitat accepts water");
    Expect(HabitatMatches(CreatureHabitat::Amphibious, aerialInWater),
           "amphibious habitat accepts water");
    Expect(!HabitatMatches(CreatureHabitat::Terrestrial, aerialInWater),
           "terrestrial habitat rejects liquid");
    EnvironmentSample lava;
    lava.inFluid = true;
    lava.inLava = true;
    Expect(HabitatMatches(CreatureHabitat::Lava, lava),
           "lava habitat accepts lava");
  }

  {
    const glm::vec3 savedOrigin(3.f, 10.f, -2.f);
    const auto adjust = [](const glm::vec3 &candidate) { return candidate; };
    const auto loaded = [](const glm::vec3 &) { return true; };
    const auto safe = [](const glm::vec3 &candidate)
    {
      return candidate.x >= 3.5f;
    };
    const auto recovered = cutum::FindSafeCreatureOrigin(
        savedOrigin, adjust, loaded, safe);
    Expect(recovered && glm::distance(*recovered,
                                      savedOrigin + glm::vec3(0.5f, 0.f, 0.f)) <
                            1e-4f,
           "loaded blocked position recovers to a nearby safe origin");

    int safeChecks = 0;
    const auto unloaded = [](const glm::vec3 &) { return false; };
    const auto shouldNotProbe = [&](const glm::vec3 &)
    {
      ++safeChecks;
      return true;
    };
    const auto deferred = cutum::FindSafeCreatureOrigin(
        savedOrigin, adjust, unloaded, shouldNotProbe);
    Expect(!deferred && safeChecks == 0,
           "unloaded terrain defers safety probes and preserves load position");
  }

  using cutum::CreaturePopulationCounts;
  using cutum::CreaturePopulationRules;
  Expect(CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{}, false, WorldDifficulty::Peaceful),
         "peaceful allows passive fauna");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{}, true, WorldDifficulty::Peaceful),
         "peaceful rejects hostile fauna");
  Expect(CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{}, false, WorldDifficulty::Normal,
             creative),
         "creative allows natural passive fauna");
  Expect(CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{}, true, WorldDifficulty::Normal,
             creative),
         "creative permits natural hostile wildlife without aggro");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{}, true, WorldDifficulty::Peaceful,
             creative),
         "creative peaceful suppresses hostile population");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{CreaturePopulationRules::NaturalWorldLimit,
                                      0, 0, 0},
             false, WorldDifficulty::Normal),
         "natural world population cap");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, CreaturePopulationRules::LocalLimit,
                                      0, 0},
             false, WorldDifficulty::Normal),
         "local population cap");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0,
                                      CreaturePopulationRules::HostileWorldLimitBase,
                                      CreaturePopulationRules::HostileLocalLimitBase},
             true, WorldDifficulty::Normal),
         "hostile population caps");
  Expect(CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0, 5, 1}, true,
             WorldDifficulty::Easy),
         "easy hostile cap permits the last available slot");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0, 6, 0}, true,
             WorldDifficulty::Easy),
         "easy hostile world cap is halved");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0, 0, 2}, true,
             WorldDifficulty::Easy),
         "easy hostile local cap is halved");
  Expect(CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0, 17, 5}, true,
             WorldDifficulty::Hard),
         "hard hostile caps scale up");
  Expect(!CreaturePopulationRules::CanAddNaturalCreature(
             CreaturePopulationCounts{0, 0, 18, 0}, true,
             WorldDifficulty::Hardcore),
         "hardcore hostile world cap matches hard");

  Expect(ModePolicy::AllowsStatusDot(survival), "survival status dot");
  Expect(!ModePolicy::AllowsStatusDot(creative), "creative no status dot");

  Expect(ModePolicy::AllowsFlight(creative, CreatureHabitat::Terrestrial),
         "creative terrestrial flight");
  Expect(!ModePolicy::AllowsFlight(survival, CreatureHabitat::Terrestrial),
         "survival terrestrial no flight");
  Expect(ModePolicy::AllowsFlight(survival, CreatureHabitat::Aerial),
         "survival aerial flight");

  Expect(ModePolicy::AllowsCreativePalette(creative), "creative palette");
  Expect(!ModePolicy::AllowsCreativePalette(survival), "survival no palette");

  Expect(ModePolicy::AllowsInstantDelete(creative), "creative instant delete");
  Expect(!ModePolicy::AllowsInstantDelete(survival), "survival no instant delete");

  Expect(ModePolicy::AllowsFreePlacement(creative), "creative free place");
  Expect(!ModePolicy::AllowsFreePlacement(survival), "survival not free place");

  Expect(!ModePolicy::ConsumesResourcesOnPlace(creative),
         "creative no consume on place");
  Expect(ModePolicy::ConsumesResourcesOnPlace(survival),
         "survival consume on place");

  Expect(!ModePolicy::DropsBlocksOnBreak(creative), "creative no drop");
  Expect(ModePolicy::DropsBlocksOnBreak(survival), "survival drop");

  Expect(ModePolicy::AllowsMobSpawnFromPalette(creative), "creative mob spawn");
  Expect(!ModePolicy::AllowsMobSpawnFromPalette(survival), "survival no mob spawn");

  Expect(ModePolicy::AllowsQaSpawner(creative), "creative qa spawner");
  Expect(!ModePolicy::AllowsQaSpawner(survival), "survival no qa spawner");

  Expect(ModePolicy::ShouldInitCreativeDefaults(creative),
         "creative init defaults");
  Expect(!ModePolicy::ShouldInitCreativeDefaults(survival),
         "survival no init defaults");

  if (Failures != 0)
  {
    std::cerr << "mode_policy_test: " << Failures << " failure(s)" << std::endl;
    return 1;
  }

  std::cout << "mode_policy_test: OK" << std::endl;
  return 0;
}
