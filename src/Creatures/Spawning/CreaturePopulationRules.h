#ifndef CREATURE_POPULATION_RULES_H
#define CREATURE_POPULATION_RULES_H

#include "Game/ModePolicy.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cutum
{

struct CreaturePopulationCounts
{
  std::size_t naturalWorld{0};
  std::size_t local{0};
  std::size_t hostileWorld{0};
  std::size_t hostileLocal{0};
};

struct CreaturePopulationRules
{
  static constexpr std::size_t NaturalWorldLimit = 64;
  static constexpr std::size_t LocalLimit = 12;
  static constexpr float LocalRadiusBlocks = 40.f;
  static constexpr std::size_t HostileWorldLimitBase = 12;
  static constexpr std::size_t HostileLocalLimitBase = 4;

  static void Accumulate(CreaturePopulationCounts &counts,
                         CreatureSpawnSource source, bool hostile, bool local)
  {
    if (CountsTowardNaturalPopulationLimit(source))
    {
      ++counts.naturalWorld;
    }
    if (hostile)
    {
      ++counts.hostileWorld;
    }
    if (local)
    {
      ++counts.local;
      if (hostile)
      {
        ++counts.hostileLocal;
      }
    }
  }

  static bool CanAddNaturalCreature(const CreaturePopulationCounts &counts,
                                    bool hostile,
                                    WorldDifficulty difficulty,
                                    WorldGameMode mode =
                                        WorldGameMode::Survival)
  {
    if (counts.naturalWorld >= NaturalWorldLimit || counts.local >= LocalLimit)
    {
      return false;
    }
    if (!hostile)
    {
      return true;
    }
    if (!ModePolicy::AllowsHostileSpawning(mode, difficulty))
    {
      return false;
    }
    const WorldDifficultyProfile profile = WorldDifficultyProfileFor(difficulty);
    const std::size_t localLimit = static_cast<std::size_t>(std::max(
        1.f, std::floor(HostileLocalLimitBase *
                        profile.HostileSpawnPressureMultiplier)));
    const std::size_t worldLimit = static_cast<std::size_t>(std::max(
        1.f, std::floor(HostileWorldLimitBase *
                        profile.HostileSpawnPressureMultiplier)));
    return counts.hostileLocal < localLimit &&
           counts.hostileWorld < worldLimit;
  }
};

} // namespace cutum

#endif
