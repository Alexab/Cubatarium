#ifndef CREATURE_COMBAT_H
#define CREATURE_COMBAT_H

#include "Creatures/Core/Creature.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

class UWorld;

/// No movement-blocking block between the attacker's eye and the target body
/// (shared by the influence resolver and the AI: mobs must not stand in
/// Attack when a wall eats every strike).
bool CreatureMeleeLineOfSight(const UWorld &world, const UCreature &source,
                              const UCreature &target);

struct CreatureCombat
{
  /// Resolve melee strike from attacker Influence intent.
  /// Returns true if target was removed.
  static bool TryMeleeStrike(UWorld &world, UCreature &attacker,
                             WorldGameMode mode);
  /// Hitscan ranged strike (bow); same apply path as melee.
  static bool TryRangedStrike(UWorld &world, UCreature &attacker,
                              WorldGameMode mode);
};

} // namespace cutum

#endif
