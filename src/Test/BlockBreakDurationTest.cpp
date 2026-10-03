#include "Blocks/BlockDigRules.h"
#include "Creatures/Influence/DigSessionState.h"
#include "Game/WorldGameMode.h"

#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_break_duration_test: " << message << std::endl;
    std::exit(1);
  }
}

static void ExpectNear(float actual, float expected, const char *message)
{
  if (std::fabs(actual - expected) > 1e-4f)
  {
    std::cerr << "block_break_duration_test: " << message
              << " actual=" << actual << " expected=" << expected << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::BlockDigRules;
  using cutum::WorldGameMode;

  ExpectNear(BlockDigRules::DigDurationSeconds(1.5f, WorldGameMode::Survival),
             1.5f * BlockDigRules::DigSecondsPerHardness,
             "stone dig duration");
  ExpectNear(BlockDigRules::DigDurationSeconds(0.5f, WorldGameMode::Survival),
             0.5f * BlockDigRules::DigSecondsPerHardness, "dirt dig duration");

  Expect(BlockDigRules::DigDurationSeconds(0.0f, WorldGameMode::Survival) < 0.0f,
         "survival hardness 0 is unbreakable");
  Expect(BlockDigRules::IsUnbreakableInSurvival(0.0f, WorldGameMode::Survival),
         "IsUnbreakableInSurvival true for hardness 0");

  ExpectNear(BlockDigRules::DigDurationSeconds(50.0f, WorldGameMode::Creative),
             0.0f, "creative ignores hardness");
  ExpectNear(BlockDigRules::DigDurationSeconds(0.0f, WorldGameMode::Creative),
             0.0f, "creative breaks hardness 0 instantly");
  Expect(!BlockDigRules::IsUnbreakableInSurvival(0.0f, WorldGameMode::Creative),
         "creative not unbreakable");

  // Tick semantics used by World::TickBreakSession:
  // duration < 0 => no progress; duration == 0 => progress = 1.
  float progress = 0.0f;
  const float unbreakable = BlockDigRules::DigDurationSeconds(
      0.0f, WorldGameMode::Survival);
  if (unbreakable < 0.0f)
  {
    // no progress
  }
  else if (unbreakable <= 0.0f)
  {
    progress = 1.0f;
  }
  Expect(progress == 0.0f, "unbreakable keeps progress at 0");

  progress = 0.0f;
  const float instant =
      BlockDigRules::DigDurationSeconds(1.0f, WorldGameMode::Creative);
  if (instant < 0.0f)
  {
  }
  else if (instant <= 0.0f)
  {
    progress = 1.0f;
  }
  Expect(progress == 1.0f, "creative instant sets progress to 1");

  // DigSessionState mirrors World break-session tick semantics.
  {
    cutum::DigSessionState session;
    session.Start(glm::ivec3(1, 2, 3));
    Expect(session.progress == 0.f, "dig session starts at 0");
    session.Tick(0.5f, 1.0f);
    ExpectNear(session.progress, 0.5f, "dig session half progress");
    session.Tick(1.0f, 1.0f);
    Expect(session.Complete(), "dig session completes");
    session.Start(glm::ivec3(0, 0, 0));
    session.Tick(0.1f, 0.0f);
    Expect(session.Complete(), "dig duration 0 completes immediately");
    session.Start(glm::ivec3(0, 0, 0));
    session.Tick(1.0f, -1.0f);
    Expect(session.progress == 0.f, "unbreakable dig duration does not progress");
  }

  std::cout << "block_break_duration_test: OK" << std::endl;
  return 0;
}
