#ifndef CREATURE_COMBAT_H
#define CREATURE_COMBAT_H

#include "Creatures/Core/Creature.h"
#include "Game/WorldGameMode.h"

namespace cutum
{

class UWorld;

struct CreatureCombat
{
  /// Resolve melee strike from attacker Influence intent.
  /// Returns true if target was removed.
  static bool TryMeleeStrike(UWorld &world, UCreature &attacker,
                             WorldGameMode mode);
};

} // namespace cutum

#endif
