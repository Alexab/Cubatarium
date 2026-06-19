#include "Pose/TerrestrialQuadrupedPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

LocomotionArchetype UTerrestrialQuadrupedPosePresenter::GetArchetype() const
{
  return LocomotionArchetype::TerrestrialQuadruped;
}

CreaturePoseParams UTerrestrialQuadrupedPosePresenter::Compute(
    const CreatureLocomotionFacts &facts, const CreatureDefinition &def,
    float /*dt*/)
{
  CreaturePoseParams pose;
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float flyPitch = def.visual.Animation.flyBodyPitchDeg;
  const float walkSpeed = std::max(def.locomotion.walkSpeed, 0.01f);
  const float swingScale =
      std::clamp(facts.horizontalSpeed / walkSpeed, 0.5f, 1.5f);

  switch (facts.state)
  {
  case LocomotionState::Walk:
  case LocomotionState::Run:
  {
    const float sinP = std::sin(facts.animPhase);
    const float legTilt = sinP * legSwing * swingScale;

    CreaturePartPose legFl;
    legFl.eulerDeg = glm::vec3(legTilt, 0.0f, 0.0f);
    CreaturePartPose legBr;
    legBr.eulerDeg = glm::vec3(legTilt, 0.0f, 0.0f);
    CreaturePartPose legFr;
    legFr.eulerDeg = glm::vec3(-legTilt, 0.0f, 0.0f);
    CreaturePartPose legBl;
    legBl.eulerDeg = glm::vec3(-legTilt, 0.0f, 0.0f);
    pose.SetPart("leg_fl", legFl);
    pose.SetPart("leg_br", legBr);
    pose.SetPart("leg_fr", legFr);
    pose.SetPart("leg_bl", legBl);
    break;
  }
  case LocomotionState::Fly:
  {
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(flyPitch, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    break;
  }
  case LocomotionState::Jump:
  {
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(-10.0f, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    break;
  }
  case LocomotionState::Fall:
  {
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(10.0f, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    break;
  }
  case LocomotionState::Crouch:
    pose.crouchUpperDrop = facts.stanceBlend;
    break;
  default:
    break;
  }

  if (facts.lookAtWeight > 0.0f)
  {
    CreaturePartPose head;
    head.eulerDeg =
        glm::vec3(facts.bodyPitch * 0.25f, 0.0f, 0.0f);
    pose.SetPart("head", head);
  }
  else if (facts.state == LocomotionState::Walk ||
           facts.state == LocomotionState::Run)
  {
    const float bob = std::sin(facts.animPhase * 2.0f) * 0.04f;
    CreaturePartPose head;
    head.offsetDelta = glm::vec3(0.0f, bob, 0.04f);
    pose.SetPart("head", head);
  }

  return pose;
}

} // namespace cutum
