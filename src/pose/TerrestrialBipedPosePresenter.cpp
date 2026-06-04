#include "TerrestrialBipedPosePresenter.h"
#include "CreatureDefinition.h"
#include <glm/glm.hpp>
#include <cmath>

namespace cutum {

LocomotionArchetype TerrestrialBipedPosePresenter::GetArchetype() const
{
 return LocomotionArchetype::TerrestrialBiped;
}

CreaturePoseParams TerrestrialBipedPosePresenter::Compute(const CreatureLocomotionFacts& facts,
                                                          const CreatureDefinition& def,
                                                          float /*dt*/)
{
 CreaturePoseParams pose;
 const float legSwing = def.visual.animation.legSwingDeg;
 const float armSwing = def.visual.animation.armSwingDeg;
 const float flyPitch = def.visual.animation.flyBodyPitchDeg;

 switch (facts.state) {
 case LocomotionState::Walk:
 case LocomotionState::Run: {
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing;
  const float armTilt = -sinP * armSwing;
  constexpr float kLegStrideBlocks = 0.05f;

  CreaturePartPose legL;
  legL.offsetDelta = glm::vec3(0.0f, 0.0f, sinP * kLegStrideBlocks);
  legL.eulerDeg = glm::vec3(legTilt, 0.0f, 0.0f);
  CreaturePartPose legR;
  legR.offsetDelta = glm::vec3(0.0f, 0.0f, -sinP * kLegStrideBlocks);
  legR.eulerDeg = glm::vec3(-legTilt, 0.0f, 0.0f);
  CreaturePartPose armL;
  armL.eulerDeg = glm::vec3(armTilt, 0.0f, 0.0f);
  CreaturePartPose armR;
  armR.eulerDeg = glm::vec3(-armTilt, 0.0f, 0.0f);
  pose.SetPart("leg_l", legL);
  pose.SetPart("leg_r", legR);
  pose.SetPart("arm_l", armL);
  pose.SetPart("arm_r", armR);
  break;
 }
 case LocomotionState::Fly: {
  CreaturePartPose torso;
  torso.eulerDeg = glm::vec3(flyPitch, 0.0f, 0.0f);
  pose.SetPart("torso", torso);
  break;
 }
 case LocomotionState::Jump: {
  CreaturePartPose torso;
  torso.eulerDeg = glm::vec3(-10.0f, 0.0f, 0.0f);
  pose.SetPart("torso", torso);
  break;
 }
 case LocomotionState::Fall: {
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

 if (facts.lookAtWeight > 0.0f) {
  const float headSway = 0.15f * facts.lookAtWeight;
  CreaturePartPose head;
  head.offsetDelta = glm::vec3(0.0f, 0.0f, headSway);
  head.eulerDeg = glm::vec3(0.0f, facts.bodyPitch * 0.25f, 0.0f);
  pose.SetPart("head", head);
 } else if (facts.state == LocomotionState::Walk || facts.state == LocomotionState::Run) {
  const float bob = std::sin(facts.animPhase * 2.0f) * 0.04f;
  CreaturePartPose head;
  head.offsetDelta = glm::vec3(0.0f, bob, 0.06f);
  pose.SetPart("head", head);
 } else if (facts.state == LocomotionState::Fly) {
  CreaturePartPose head;
  head.offsetDelta = glm::vec3(0.0f, 0.0f, 0.1f);
  pose.SetPart("head", head);
 }

 return pose;
}

} // namespace cutum
