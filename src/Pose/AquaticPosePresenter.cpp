#include "Pose/AquaticPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <cmath>

namespace cutum
{

LocomotionArchetype UAquaticPosePresenter::GetArchetype() const
{
  return LocomotionArchetype::Aquatic;
}

CreaturePoseParams UAquaticPosePresenter::Compute(
    const CreatureLocomotionFacts &facts, const CreatureDefinition &def,
    float /*dt*/)
{
  CreaturePoseParams pose;
  const float tailSwing = def.visual.Animation.tailSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float sinP = std::sin(facts.animPhase);

  switch (facts.state)
  {
  case LocomotionState::Swim:
  case LocomotionState::Tread:
  {
    CreaturePartPose torso;
    torso.offsetDelta = glm::vec3(0.0f, sinP * bodyBob, 0.0f);
    torso.eulerDeg = glm::vec3(sinP * 4.0f, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    pose.SetPart("body", torso);

    CreaturePartPose tail;
    tail.eulerDeg = glm::vec3(0.0f, sinP * tailSwing, 0.0f);
    pose.SetPart("tail", tail);

    const float finTilt = sinP * def.visual.Animation.legSwingDeg * 0.6f;
    CreaturePartPose finL;
    finL.eulerDeg = glm::vec3(finTilt, 0.0f, 0.0f);
    CreaturePartPose finR;
    finR.eulerDeg = glm::vec3(-finTilt, 0.0f, 0.0f);
    pose.SetPart("fin_l", finL);
    pose.SetPart("fin_r", finR);
    pose.SetPart("leg_fl", finL);
    pose.SetPart("leg_fr", finR);
    break;
  }
  default:
    break;
  }
  return pose;
}

} // namespace cutum
