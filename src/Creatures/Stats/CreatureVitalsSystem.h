#ifndef CREATURE_VITALS_SYSTEM_H
#define CREATURE_VITALS_SYSTEM_H

#include "Game/WorldDifficulty.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

class UWorld;
class UCreature;

struct CreatureVitalsSystem
{
  /// Survival-only needs/breath/fatigue tick for creatures with NeedsNeedsTick.
  /// Difficulty scales drain rates (ignored in Creative — Tick no-ops).
  static void Tick(UWorld &world, WorldGameMode mode, WorldDifficulty difficulty,
                   float dt);

  /// Apply damage; returns true if the creature should be removed (permadeath).
  static bool ApplyDamage(UWorld &world, UCreature &target, float amount,
                          WorldGameMode mode);

  /// Handle health<=0: fatal wound / respawn / despawn. Returns true if removed.
  static bool HandleLethal(UWorld &world, UCreature &target, WorldGameMode mode);
};

} // namespace cutum

#endif
