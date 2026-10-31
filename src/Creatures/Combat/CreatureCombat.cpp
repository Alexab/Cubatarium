#include "Creatures/Combat/CreatureCombat.h"
#include "Items/ItemToolInfluenceProvider.h"
#include "Creatures/Core/CreatureReach.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "World/Core/World.h"
#include "World/Raycast/BlockRaycast.h"

namespace cutum
{

bool CreatureMeleeLineOfSight(const UWorld &world, const UCreature &source,
                              const UCreature &target)
{
  const UBlockWorld &blocks = world.GetBlockWorld();
  const UBlockRegistry &registry = world.GetBlockRegistry();
  const auto segmentClear = [&](const glm::vec3 &from, const glm::vec3 &to)
  {
    const glm::vec3 delta = to - from;
    const float len = glm::length(delta);
    if (len < 1e-4f)
    {
      return true;
    }
    return !RaycastSolidBlocks(blocks, registry, from, delta, len).has_value();
  };
  return HasMeleeLineOfSight(source.GetEyePosition(), target.GetBodyOrigin(),
                             target.GetBounds().profile.restSizeBlocks,
                             segmentClear);
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

bool CreatureCombat::TryRangedStrike(UWorld &world, UCreature &attacker,
                                     WorldGameMode mode)
{
  // Deliberate alias: "ranged" currently resolves through the same hitscan
  // path as melee; real projectiles are backlog (CREATURES_AUDIT_2026-10_RU
  // finding I2).
  return TryMeleeStrike(world, attacker, mode);
}

} // namespace cutum
