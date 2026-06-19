#include "Pose/TerrestrialQuadrupedPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

namespace
{

constexpr float kPi = 3.14159265f;

void ApplyHeadFamilyPose(CreaturePoseParams &pose, const CreaturePartPose &headPose)
{
  pose.SetPart("head", headPose);
  pose.SetPart("snout", headPose);
  pose.SetPart("beak", headPose);
}

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
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float tailSwing = def.visual.Animation.tailSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);

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

    const float spineOffset = 0.18f * kPi;
    CreaturePartPose torso;
    torso.offsetDelta = glm::vec3(
        0.0f,
        std::sin(facts.animPhase * 2.0f + spineOffset) * bodyBob * swingScale,
        0.0f);
    pose.SetPart("torso", torso);

    CreaturePartPose tail;
    tail.eulerDeg =
        glm::vec3(std::sin(facts.animPhase + kPi * 0.5f) * tailSwing, 0.0f,
                  0.0f);
    pose.SetPart("tail", tail);
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
  {
    const float slowWag = std::sin(facts.animPhase * 0.4f) * tailSwing * 0.35f;
    CreaturePartPose tail;
    tail.eulerDeg = glm::vec3(slowWag, 0.0f, 0.0f);
    pose.SetPart("tail", tail);

    const float earTwitch = std::sin(facts.animPhase * 0.7f) * 3.0f;
    CreaturePartPose earL;
    earL.eulerDeg = glm::vec3(0.0f, earTwitch, 0.0f);
    CreaturePartPose earR;
    earR.eulerDeg = glm::vec3(0.0f, -earTwitch, 0.0f);
    pose.SetPart("ear_l", earL);
    pose.SetPart("ear_r", earR);
    break;
  }
  }

  if (facts.lookAtWeight > 0.0f)
  {
    CreaturePartPose head;
    head.eulerDeg =
        glm::vec3(facts.bodyPitch * 0.25f, 0.0f, 0.0f);
    ApplyHeadFamilyPose(pose, head);
  }
  else if (facts.state == LocomotionState::Walk ||
           facts.state == LocomotionState::Run)
  {
    const float bob = std::sin(facts.animPhase * 2.0f) * bodyBob * swingScale;
    CreaturePartPose head;
    head.offsetDelta = glm::vec3(0.0f, bob, 0.04f);
    head.eulerDeg =
        glm::vec3(std::sin(facts.animPhase) * 4.0f * swingScale, 0.0f, 0.0f);
    ApplyHeadFamilyPose(pose, head);
  }

  return pose;
}

} // namespace cutum
