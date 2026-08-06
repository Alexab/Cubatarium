#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Game/WorldGameMode.h"
#include "World/Core/World.h"
#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

float PlayerInteractionRouter::MeleePickReachBlocks()
{
  // Match InfluenceResolver reach slack (RangeBlocks + 0.35).
  return InfluenceCapability::DefaultBareHand(1).RangeBlocks + 0.35f;
}

bool PlayerInteractionRouter::TryRouteMeleeFromView(UWorld &world,
                                                   UCreature &controlled,
                                                   const glm::vec3 &eye,
                                                   const glm::vec3 &front)
{
  if (world.GetGameMode() != WorldGameMode::Survival)
  {
    return false;
  }
  const auto target =
      world.PickCreatureByView(eye, front, MeleePickReachBlocks());
  if (!target || *target == controlled.GetId())
  {
    return false;
  }
  SetMeleeIntent(controlled, *target);
  return true;
}

void PlayerInteractionRouter::BeginDigIntent(UCreature &controlled,
                                             glm::ivec3 blockPos)
{
  CreatureIntent intent = controlled.GetIntent();
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Dig;
  intent.Influence.TargetBlockPos = blockPos;
  intent.Influence.HasTargetBlock = true;
  intent.suggestedAnim = LocomotionState::Action;
  intent.clearOnApply = false;
  controlled.SetIntent(intent);
}

void PlayerInteractionRouter::ClearDigIntent(UCreature &controlled)
{
  CreatureIntent intent = controlled.GetIntent();
  if (intent.Influence.Channel == InfluenceChannel::Dig)
  {
    intent.Influence = InfluenceIntent{};
    controlled.SetIntent(intent);
  }
}

void PlayerInteractionRouter::SetMeleeIntent(UCreature &attacker,
                                             CreatureId targetId)
{
  CreatureIntent intent = attacker.GetIntent();
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Melee;
  intent.Influence.TargetId = targetId;
  intent.suggestedAnim = LocomotionState::Action;
  intent.clearOnApply = false;
  attacker.SetIntent(intent);
}

void PlayerInteractionRouter::SetUseIntent(UCreature &controlled)
{
  CreatureIntent intent = controlled.GetIntent();
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Use;
  intent.Influence.TargetId = controlled.GetId();
  intent.suggestedAnim = LocomotionState::Action;
  intent.clearOnApply = true;
  controlled.SetIntent(intent);
}

void PlayerInteractionRouter::SetRangedIntent(UCreature &attacker,
                                              CreatureId targetId)
{
  CreatureIntent intent = attacker.GetIntent();
  intent.attackTargetId = 0;
  intent.Influence = InfluenceIntent{};
  intent.Influence.Channel = InfluenceChannel::Ranged;
  intent.Influence.TargetId = targetId;
  intent.suggestedAnim = LocomotionState::Action;
  intent.clearOnApply = false;
  attacker.SetIntent(intent);
}

bool PlayerInteractionRouter::TryRouteRangedFromView(UWorld &world,
                                                     UCreature &controlled,
                                                     const glm::vec3 &eye,
                                                     const glm::vec3 &front,
                                                     float rangeBlocks,
                                                     bool requireLos)
{
  if (world.GetGameMode() != WorldGameMode::Survival)
  {
    return false;
  }
  const float reach = std::max(2.f, rangeBlocks) + 0.35f;
  const auto target = world.PickCreatureByView(eye, front, reach);
  if (!target || *target == controlled.GetId())
  {
    return false;
  }
  if (requireLos)
  {
    const UCreature *creature = world.GetCreature(*target);
    if (!creature)
    {
      return false;
    }
    const glm::vec3 toTarget = creature->GetBodyOrigin() - eye;
    const float dist = glm::length(toTarget);
    if (dist > 1e-3f)
    {
      const glm::vec3 dir = toTarget / dist;
      const float maxCheck = std::max(0.f, dist - 0.35f);
      if (maxCheck > 0.05f)
      {
        if (const auto blockHit = RaycastSolidBlocks(
                world.GetBlockWorld(), world.GetBlockRegistry(), eye, dir,
                maxCheck))
        {
          return false;
        }
      }
    }
  }
  SetRangedIntent(controlled, *target);
  return true;
}

} // namespace cutum
