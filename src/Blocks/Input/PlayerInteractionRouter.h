#ifndef PLAYER_INTERACTION_ROUTER_H
#define PLAYER_INTERACTION_ROUTER_H

#include "Creatures/Core/Creature.h"
#include "Game/WorldGameMode.h"
#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

/// Routes player LMB into InfluenceIntent (Melee vs Dig). Gesture timing and
/// hold-min stay in BlockInputController; this owns dig/combat SoT writes.
struct PlayerInteractionRouter
{
  /// Melee pick reach ≤ strike reach (RangeBlocks + resolver slack).
  static float MeleePickReachBlocks();

  /// Survival: if a creature is under the crosshair within pick reach, set
  /// Melee InfluenceIntent on `controlled` and return true (caller skips dig).
  static bool TryRouteMeleeFromView(UWorld &world, UCreature &controlled,
                                    const glm::vec3 &eye,
                                    const glm::vec3 &front);

  /// Arm Dig InfluenceIntent for `blockPos` (session starts in Dig Apply).
  static void BeginDigIntent(UCreature &controlled, glm::ivec3 blockPos);

  /// Clear Dig channel on controlled creature if active.
  static void ClearDigIntent(UCreature &controlled);

  /// Write Melee InfluenceIntent only (no legacy attackTargetId).
  static void SetMeleeIntent(UCreature &attacker, CreatureId targetId);

  /// Self-use (eat/drink) InfluenceIntent on controlled creature.
  static void SetUseIntent(UCreature &controlled);

  /// Hitscan ranged (bow): pick within `rangeBlocks`.
  /// When `requireLos`, reject if a solid block is closer than the target.
  static bool TryRouteRangedFromView(UWorld &world, UCreature &controlled,
                                     const glm::vec3 &eye,
                                     const glm::vec3 &front, float rangeBlocks,
                                     bool requireLos = true);
  static void SetRangedIntent(UCreature &attacker, CreatureId targetId);
};

} // namespace cutum

#endif
