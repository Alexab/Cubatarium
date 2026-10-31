#ifndef CREATURE_VITALS_SYSTEM_H
#define CREATURE_VITALS_SYSTEM_H

#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"
#include <cstdint>

namespace cutum
{

using CreatureId = uint64_t;

class UWorld;
class UCreature;

struct CreatureVitalsSystem
{
  /// Survival-only needs/breath/fatigue tick for creatures with NeedsNeedsTick.
  /// Difficulty scales drain rates (ignored in Creative — Tick no-ops).
  static void Tick(UWorld &world, WorldGameMode mode, WorldDifficulty difficulty,
                   float dt);

  /// Apply damage; returns true if the creature should be removed (permadeath).
  /// `reason` is a tag for logs / Influence (e.g. "influence", "starve").
  /// `attackerId` feeds the victim's AI damage memory (flee / aggro).
  static bool ApplyDamage(UWorld &world, UCreature &target, float amount,
                          WorldGameMode mode, const char *reason = nullptr,
                          CreatureId attackerId = 0);

  /// Handle health<=0: fatal wound / respawn / despawn. Returns true if removed.
  static bool HandleLethal(UWorld &world, UCreature &target, WorldGameMode mode);
};

} // namespace cutum

#endif
