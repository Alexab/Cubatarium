#include "Pose/TerrestrialBipedPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

namespace
{

float WalkSwingScale(const CreatureLocomotionFacts &facts,
                     const CreatureDefinition &def)
{
  const float walkSpeed = std::max(def.locomotion.walkSpeed, 0.01f);
  float swingScale =
      std::clamp(facts.horizontalSpeed / walkSpeed, 0.5f, 1.5f);
  if (facts.state == LocomotionState::Run &&
      facts.horizontalSpeed > walkSpeed * 1.2f)
  {
    swingScale *= def.visual.Animation.runSpeedMultiplier;
  }
  return swingScale;
}

} // namespace

LocomotionArchetype UTerrestrialBipedPosePresenter::GetArchetype() const
{
  return LocomotionArchetype::TerrestrialBiped;
}

CreaturePoseParams
UTerrestrialBipedPosePresenter::Compute(const CreatureLocomotionFacts &facts,
                                        const CreatureDefinition &def,
                                        float /*dt*/)
{
  CreaturePoseParams pose;
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float armSwing = def.visual.Animation.armSwingDeg;
  const float flyPitch = def.visual.Animation.flyBodyPitchDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float crouchLegBend = def.visual.Animation.crouchLegBendDeg;
  const float swingScale = WalkSwingScale(facts, def);

  switch (facts.state)
  {
  case LocomotionState::Walk:
  case LocomotionState::Run:
  {
    const float sinP = std::sin(facts.animPhase);
    const float legTilt = sinP * legSwing * swingScale;
    const float armTilt = -sinP * armSwing * swingScale;

    CreaturePartPose legL;
    legL.eulerDeg = glm::vec3(legTilt, 0.0f, 0.0f);
    CreaturePartPose legR;
    legR.eulerDeg = glm::vec3(-legTilt, 0.0f, 0.0f);
    CreaturePartPose armL;
    armL.eulerDeg = glm::vec3(armTilt, 0.0f, 0.0f);
    CreaturePartPose armR;
    armR.eulerDeg = glm::vec3(-armTilt, 0.0f, 0.0f);
    pose.SetPart("leg_l", legL);
    pose.SetPart("leg_r", legR);
    pose.SetPart("arm_l", armL);
    pose.SetPart("arm_r", armR);

    CreaturePartPose torso;
    torso.offsetDelta = glm::vec3(
        0.0f, std::sin(facts.animPhase * 2.0f) * bodyBob * swingScale, 0.0f);
    pose.SetPart("torso", torso);
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
  {
    pose.crouchUpperDrop = facts.stanceBlend;
    const float kneeBend = crouchLegBend * facts.stanceBlend;
    CreaturePartPose legL;
    legL.eulerDeg = glm::vec3(kneeBend, 0.0f, 0.0f);
    CreaturePartPose legR;
    legR.eulerDeg = glm::vec3(kneeBend, 0.0f, 0.0f);
    pose.SetPart("leg_l", legL);
    pose.SetPart("leg_r", legR);
    break;
  }
  default:
  {
    const float idleSway = std::sin(facts.animPhase * 0.5f) * 2.0f;
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(idleSway, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    break;
  }
  }

  if (facts.lookAtWeight > 0.0f)
  {
    const float headSway = 0.15f * facts.lookAtWeight;
    CreaturePartPose head;
    head.offsetDelta = glm::vec3(0.0f, 0.0f, headSway);
    head.eulerDeg = glm::vec3(facts.bodyPitch * 0.25f, 0.0f, 0.0f);
    pose.SetPart("head", head);
  }
  else if (std::abs(facts.bodyPitch) > 0.01f &&
           facts.state != LocomotionState::Walk &&
           facts.state != LocomotionState::Run)
  {
    CreaturePartPose head;
    head.eulerDeg = glm::vec3(facts.bodyPitch * 0.5f, 0.0f, 0.0f);
    pose.SetPart("head", head);
  }
  else if (facts.state == LocomotionState::Walk ||
           facts.state == LocomotionState::Run)
  {
    const float bob = std::sin(facts.animPhase * 2.0f) * bodyBob * swingScale;
    CreaturePartPose head;
    head.offsetDelta = glm::vec3(0.0f, bob, 0.06f);
    pose.SetPart("head", head);
  }
  else if (facts.state == LocomotionState::Fly)
  {
    CreaturePartPose head;
    head.offsetDelta = glm::vec3(0.0f, 0.0f, 0.1f);
    pose.SetPart("head", head);
  }

  return pose;
}

} // namespace cutum
