#include "Creatures/Combat/CreatureCombat.h"
#include "Items/ItemToolInfluenceProvider.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "World/Core/World.h"

namespace cutum
{

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

bool CreatureCombat::TryRangedStrike(UWorld &world, UCreature &attacker,
                                     WorldGameMode mode)
{
  return TryMeleeStrike(world, attacker, mode);
}

} // namespace cutum
