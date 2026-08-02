#include "Creatures/Combat/CreatureCombat.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

float CreatureCombat::ComputeMeleeDamage(const UCreature &attacker)
{
  const int strength = attacker.GetAttributes().strength;
  const float base = 8.f;
  return std::max(1.f, base * (0.5f + static_cast<float>(strength) / 20.f));
}

bool CreatureCombat::TryMeleeStrike(UWorld &world, UCreature &attacker,
                                    WorldGameMode mode)
{
  if (mode == WorldGameMode::Creative)
  {
    return false;
  }
  const CreatureId targetId = attacker.GetIntent().attackTargetId;
  if (targetId == 0 || targetId == attacker.GetId())
  {
    return false;
  }
  UCreature *target = world.GetCreature(targetId);
  if (!target)
  {
    return false;
  }
  const float damage = ComputeMeleeDamage(attacker);
  // Combat fatigue cost on the attacker (endurance-scaled).
  {
    CreatureVitals &av = attacker.GetVitals();
    const float endMul =
        1.f /
        std::max(0.5f,
                 0.5f + static_cast<float>(attacker.GetAttributes().endurance) /
                            20.f);
    av.fatigue = std::min(av.maxFatigue, av.fatigue + 6.f * endMul);
    av.ClampCurrents();
  }
  return CreatureVitalsSystem::ApplyDamage(world, *target, damage, mode);
}

} // namespace cutum
