#include "Game/ModePolicy.h"

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
  using cutum::CreatureHabitat;
  using cutum::ModePolicy;
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
