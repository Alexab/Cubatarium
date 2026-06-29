#include "Pose/Skeletal/SkeletalBonePoseEngine.h"

#include "Pose/Skeletal/SkeletalAnimationProfiles.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr float kPi = 3.14159265f;

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

void ApplyQuadruped(SkeletalCreaturePose &pose,
                    const CreatureLocomotionFacts &facts,
                    const CreatureDefinition &def)
{
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float tailSwing = def.visual.Animation.tailSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing * swingScale;

  SkeletalBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  SkeletalBonePose leg1;
  leg1.rotationDeg.x = legTilt;
  pose.bones["leg1"] = leg1;
  SkeletalBonePose leg2;
  leg2.rotationDeg.x = -legTilt;
  pose.bones["leg2"] = leg2;
  SkeletalBonePose leg3;
  leg3.rotationDeg.x = -legTilt;
  pose.bones["leg3"] = leg3;

  SkeletalBonePose body;
  body.offsetBlocks.y =
      std::sin(facts.animPhase * 2.f + 0.18f * kPi) * bodyBob * swingScale;
  pose.bones["body"] = body;

  SkeletalBonePose head;
  head.rotationDeg.x = std::sin(facts.animPhase * 0.5f) * 5.f;
  pose.bones["head"] = head;

  SkeletalBonePose tail;
  tail.rotationDeg.x = std::sin(facts.animPhase + kPi * 0.5f) * tailSwing;
  pose.bones["tail"] = tail;
}

void ApplyFox(SkeletalCreaturePose &pose, const CreatureLocomotionFacts &facts,
              const CreatureDefinition &def)
{
  // Start from the default quadruped body/head placement, then override fox-specific gait.
  ApplyQuadruped(pose, facts, def);

  const float legSwing = def.visual.Animation.legSwingDeg;
  const float tailSwing = def.visual.Animation.tailSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing * swingScale;

  // Fox: rear-left + front-right swing together, opposite diagonal counters.
  SkeletalBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  SkeletalBonePose leg1;
  leg1.rotationDeg.x = -legTilt;
  pose.bones["leg1"] = leg1;
  SkeletalBonePose leg2;
  leg2.rotationDeg.x = -legTilt;
  pose.bones["leg2"] = leg2;
  SkeletalBonePose leg3;
  leg3.rotationDeg.x = legTilt;
  pose.bones["leg3"] = leg3;

  SkeletalBonePose tail;
  tail.rotationDeg.x = -35.0f + std::sin(facts.animPhase + kPi * 0.5f) *
                                    tailSwing * 0.45f;
  pose.bones["tail"] = tail;
}

void ApplyHumanoid(SkeletalCreaturePose &pose,
                   const CreatureLocomotionFacts &facts,
                   const CreatureDefinition &def)
{
  const float armSwing = def.visual.Animation.armSwingDeg;
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float armTilt = sinP * armSwing * swingScale;
  const float legTilt = sinP * legSwing * swingScale;

  SkeletalBonePose rightArm;
  rightArm.rotationDeg.x = -armTilt;
  pose.bones["rightArm"] = rightArm;
  SkeletalBonePose leftArm;
  leftArm.rotationDeg.x = armTilt;
  pose.bones["leftArm"] = leftArm;
  SkeletalBonePose rightLeg;
  rightLeg.rotationDeg.x = legTilt;
  pose.bones["rightLeg"] = rightLeg;
  SkeletalBonePose leftLeg;
  leftLeg.rotationDeg.x = -legTilt;
  pose.bones["leftLeg"] = leftLeg;

  SkeletalBonePose waist;
  waist.offsetBlocks.y =
      std::sin(facts.animPhase * 2.f) * def.visual.Animation.bodyBobBlocks *
      swingScale;
  pose.bones["waist"] = waist;
}

void ApplyChicken(SkeletalCreaturePose &pose,
                  const CreatureLocomotionFacts &facts,
                  const CreatureDefinition &def)
{
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float wingSwing = def.visual.Animation.wingIdleSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing * swingScale;

  SkeletalBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  SkeletalBonePose leg1;
  leg1.rotationDeg.x = -legTilt;
  pose.bones["leg1"] = leg1;

  SkeletalBonePose wing0;
  wing0.rotationDeg.z = -wingSwing;
  pose.bones["wing0"] = wing0;
  SkeletalBonePose wing1;
  wing1.rotationDeg.z = wingSwing;
  pose.bones["wing1"] = wing1;

  if (facts.state == LocomotionState::Idle)
  {
    SkeletalBonePose head;
    head.rotationDeg.x = std::sin(facts.animPhase * 3.f) * 12.f;
    pose.bones["head"] = head;
  }
}

void ApplyAerial(SkeletalCreaturePose &pose,
                 const CreatureLocomotionFacts &facts,
                 const CreatureDefinition &def)
{
  const float pitch = def.visual.Animation.flyBodyPitchDeg;
  SkeletalBonePose body;
  body.rotationDeg.x = pitch;
  pose.bones["body"] = body;
  const float flap = std::sin(facts.animPhase * 4.f) * 25.f;
  SkeletalBonePose wing0;
  wing0.rotationDeg.z = flap;
  pose.bones["wing0"] = wing0;
  SkeletalBonePose wing1;
  wing1.rotationDeg.z = -flap;
  pose.bones["wing1"] = wing1;
}

void ApplyAquatic(SkeletalCreaturePose &pose,
                  const CreatureLocomotionFacts &facts,
                  const CreatureDefinition &def)
{
  const float swingScale = WalkSwingScale(facts, def);
  SkeletalBonePose body;
  body.rotationDeg.y = std::sin(facts.animPhase) * 8.f * swingScale;
  pose.bones["body"] = body;
  SkeletalBonePose tail;
  tail.rotationDeg.y = std::sin(facts.animPhase + kPi * 0.5f) * 15.f * swingScale;
  pose.bones["tail"] = tail;
}

void ApplySerpentine(SkeletalCreaturePose &pose,
                     const CreatureLocomotionFacts &facts,
                     const CreatureDefinition &def)
{
  const float swingScale = WalkSwingScale(facts, def);
  SkeletalBonePose body;
  body.rotationDeg.z = std::sin(facts.animPhase) * 6.f * swingScale;
  pose.bones["body"] = body;
}

} // namespace

SkeletalCreaturePose
SkeletalBonePoseEngine::Compute(const CreatureLocomotionFacts &facts,
                               const CreatureDefinition &def, float /*dt*/)
{
  SkeletalCreaturePose pose;
  const std::string profile = SkeletalAnimationProfiles::ResolveProfileId(def);

  switch (facts.state)
  {
  case LocomotionState::Walk:
  case LocomotionState::Run:
    if (profile == "fox")
    {
      ApplyFox(pose, facts, def);
    }
    else if (profile == "humanoid")
    {
      ApplyHumanoid(pose, facts, def);
    }
    else if (profile == "chicken")
    {
      ApplyChicken(pose, facts, def);
    }
    else if (profile == "aquatic")
    {
      ApplyAquatic(pose, facts, def);
    }
    else if (profile == "aerial")
    {
      ApplyAerial(pose, facts, def);
    }
    else if (profile == "serpentine")
    {
      ApplySerpentine(pose, facts, def);
    }
    else
    {
      ApplyQuadruped(pose, facts, def);
    }
    break;
  case LocomotionState::Fly:
    ApplyAerial(pose, facts, def);
    break;
  case LocomotionState::Idle:
  default:
    if (profile == "fox")
    {
      ApplyFox(pose, facts, def);
    }
    else if (profile == "chicken")
    {
      ApplyChicken(pose, facts, def);
    }
    else if (profile == "humanoid")
    {
      SkeletalBonePose head;
      head.rotationDeg.x = std::sin(facts.animPhase * 0.3f) * 8.f;
      pose.bones["head"] = head;
    }
  }
  return pose;
}

} // namespace cutum
