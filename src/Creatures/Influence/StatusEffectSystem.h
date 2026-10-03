#ifndef STATUS_EFFECT_SYSTEM_H
#define STATUS_EFFECT_SYSTEM_H

#include "Game/WorldGameMode.h"
#include <string>

namespace cutum
{

class UWorld;
class UCreature;

struct StatusEffectSystem
{
  static void ApplyStatus(UCreature &target, const std::string &def_id);
  static void Tick(UWorld &world, WorldGameMode mode, float dt);
  /// Aggregate move-speed multiplier from active statuses (1 = unchanged).
  static float GetMoveSpeedMultiplier(const UCreature &creature);
};

} // namespace cutum

#endif
