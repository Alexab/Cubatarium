#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "World/Core/World.h"

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

} // namespace cutum
