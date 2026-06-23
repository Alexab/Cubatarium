#include "Pose/AerialPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

namespace
{

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.283185307f;

float Smoothstep(float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void ApplyBirdHeadFamilyPose(CreaturePoseParams &pose,
                             const CreaturePartPose &headPose)
{
  pose.SetPart("head", headPose);
  pose.SetPart("beak", headPose);
  pose.SetPart("comb", headPose);
  CreaturePartPose neck;
  neck.eulerDeg = headPose.eulerDeg * 0.5f;
  neck.offsetDelta = headPose.offsetDelta * 0.5f;
  pose.SetPart("neck", neck);
}

void ApplyGroundBirdWalk(CreaturePoseParams &pose,
                         const CreatureLocomotionFacts &facts,
                         const CreatureDefinition &def, float phase)
{
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float wingIdle = def.visual.Animation.wingIdleSwingDeg;
  const float walkSpeed = std::max(def.locomotion.walkSpeed, 0.01f);
  float swingScale =
      std::clamp(facts.horizontalSpeed / walkSpeed, 0.5f, 1.5f);
  if (facts.state == LocomotionState::Run &&
      facts.horizontalSpeed > walkSpeed * 1.2f)
  {
    swingScale *= def.visual.Animation.runSpeedMultiplier;
  }

  const float sinP = std::sin(phase);
  const float legTilt = sinP * legSwing * swingScale;

  CreaturePartPose legL;
  legL.eulerDeg = glm::vec3(legTilt, 0.0f, 0.0f);
  CreaturePartPose legR;
  legR.eulerDeg = glm::vec3(-legTilt, 0.0f, 0.0f);
  pose.SetPart("leg_l", legL);
  pose.SetPart("leg_r", legR);

  const float wingBase = -8.0f;
  CreaturePartPose wingL;
  wingL.eulerDeg =
      glm::vec3(wingBase + sinP * wingIdle, 0.0f, 0.0f);
  CreaturePartPose wingR;
  wingR.eulerDeg =
      glm::vec3(wingBase - sinP * wingIdle, 0.0f, 0.0f);
  pose.SetPart("wing_l", wingL);
  pose.SetPart("wing_r", wingR);

  CreaturePartPose torso;
  torso.offsetDelta =
      glm::vec3(0.0f, std::sin(phase * 2.0f) * bodyBob * swingScale, 0.0f);
  pose.SetPart("torso", torso);

  const float bob = std::sin(phase * 2.0f) * bodyBob * swingScale;
  CreaturePartPose head;
  head.offsetDelta = glm::vec3(0.0f, bob, 0.04f);
  ApplyBirdHeadFamilyPose(pose, head);
}

void ApplyGroundBirdIdle(CreaturePoseParams &pose, float phase)
{
  const float idleFlap = std::sin(phase * 0.5f) * 5.0f;
  CreaturePartPose wingL;
  wingL.eulerDeg = glm::vec3(idleFlap, 0.0f, 0.0f);
  CreaturePartPose wingR;
  wingR.eulerDeg = glm::vec3(-idleFlap, 0.0f, 0.0f);
  pose.SetPart("wing_l", wingL);
  pose.SetPart("wing_r", wingR);

  const float cycle = phase / kTwoPi;
  const float frac = cycle - std::floor(cycle);
  float peckPitch = 0.0f;
  if (frac > 0.72f && frac < 0.92f)
  {
    const float t = (frac - 0.72f) / 0.2f;
    peckPitch = 15.0f * std::sin(t * kPi) * Smoothstep(t);
  }
  if (peckPitch > 0.01f)
  {
    CreaturePartPose head;
    head.eulerDeg = glm::vec3(peckPitch, 0.0f, 0.0f);
    ApplyBirdHeadFamilyPose(pose, head);
  }
}

void ApplyFlyingPose(CreaturePoseParams &pose, const CreatureDefinition &def,
                     float phase, float flapDeg)
{
  const float sinP = std::sin(phase);
  CreaturePartPose wingL;
  wingL.eulerDeg = glm::vec3(sinP * flapDeg, 0.0f, 0.0f);
  CreaturePartPose wingR;
  wingR.eulerDeg = glm::vec3(-sinP * flapDeg, 0.0f, 0.0f);
  pose.SetPart("wing_l", wingL);
  pose.SetPart("wing_r", wingR);

  CreaturePartPose torso;
  torso.eulerDeg =
      glm::vec3(def.visual.Animation.flyBodyPitchDeg * 0.5f, 0.0f, 0.0f);
  pose.SetPart("torso", torso);
}

} // namespace

LocomotionArchetype UAerialPosePresenter::GetArchetype() const
{
  return LocomotionArchetype::Aerial;
}

CreaturePoseParams
UAerialPosePresenter::Compute(const CreatureLocomotionFacts &facts,
                              const CreatureDefinition &def, float /*dt*/)
{
  CreaturePoseParams pose;
  const float flapDeg = 22.0f;
  const float phase = facts.animPhase;
  const bool groundBird = !def.locomotion.canFly && facts.onGround;

  if (groundBird)
  {
    switch (facts.state)
    {
    case LocomotionState::Walk:
    case LocomotionState::Run:
      ApplyGroundBirdWalk(pose, facts, def, phase);
      break;
    default:
      ApplyGroundBirdIdle(pose, phase);
      break;
    }
  }
  else
  {
    switch (facts.state)
    {
    case LocomotionState::Walk:
    case LocomotionState::Run:
    case LocomotionState::Fly:
    case LocomotionState::Hover:
    case LocomotionState::Glide:
      ApplyFlyingPose(pose, def, phase, flapDeg);
      break;
    default:
    {
      const float idleFlap = std::sin(phase * 0.5f) * flapDeg * 0.35f;
      CreaturePartPose wingL;
      wingL.eulerDeg = glm::vec3(idleFlap, 0.0f, 0.0f);
      CreaturePartPose wingR;
      wingR.eulerDeg = glm::vec3(-idleFlap, 0.0f, 0.0f);
      pose.SetPart("wing_l", wingL);
      pose.SetPart("wing_r", wingR);
      break;
    }
    }
  }

  if (facts.lookAtWeight > 0.0f || std::abs(facts.bodyPitch) > 0.01f)
  {
    CreaturePartPose head;
    head.eulerDeg = glm::vec3(facts.bodyPitch * 0.35f, 0.0f, 0.0f);
    ApplyBirdHeadFamilyPose(pose, head);
  }

  return pose;
}

} // namespace cutum
