#include "Creatures/Combat/CreatureCombat.h"
#include "Items/ItemToolInfluenceProvider.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
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
  UItemToolInfluenceProvider tools(world.GetItemDefinitionStorage());
  InfluencePrediction pred =
      InfluenceResolver::Resolve(world, attacker, mode, &tools);
  if (!pred.Valid || pred.Cancelled)
  {
    return false;
  }
  const InfluenceApplyResult result =
      InfluenceApplier::Apply(world, pred, mode);
  return result.AnyTargetRemoved;
}

} // namespace cutum
