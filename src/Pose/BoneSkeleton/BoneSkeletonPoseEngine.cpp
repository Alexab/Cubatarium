#include "Pose/BoneSkeleton/BoneSkeletonPoseEngine.h"

#include "Pose/BoneSkeleton/SkeletalAnimationProfiles.h"
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

void ApplyQuadruped(BoneSkeletonPose &pose,
                    const CreatureLocomotionFacts &facts,
                    const CreatureDefinition &def)
{
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float tailSwing = def.visual.Animation.tailSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing * swingScale;

  BoneSkeletonBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  BoneSkeletonBonePose leg1;
  leg1.rotationDeg.x = legTilt;
  pose.bones["leg1"] = leg1;
  BoneSkeletonBonePose leg2;
  leg2.rotationDeg.x = -legTilt;
  pose.bones["leg2"] = leg2;
  BoneSkeletonBonePose leg3;
  leg3.rotationDeg.x = -legTilt;
  pose.bones["leg3"] = leg3;

  BoneSkeletonBonePose body;
  body.offsetBlocks.y =
      std::sin(facts.animPhase * 2.f + 0.18f * kPi) * bodyBob * swingScale;
  pose.bones["body"] = body;

  BoneSkeletonBonePose head;
  head.rotationDeg.x = std::sin(facts.animPhase * 0.5f) * 5.f;
  pose.bones["head"] = head;

  BoneSkeletonBonePose tail;
  tail.rotationDeg.x = std::sin(facts.animPhase + kPi * 0.5f) * tailSwing;
  pose.bones["tail"] = tail;
}

void ApplyArachnid(BoneSkeletonPose &pose,
                   const CreatureLocomotionFacts &facts,
                   const CreatureDefinition &def)
{
  // Spider legs are long cubes along +X/−X; X-roll is nearly invisible.
  // Z lifts the leg in the vertical plane; light Y adds stride.
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float swingScale = WalkSwingScale(facts, def);
  const float amp = legSwing * swingScale;

  static constexpr const char *kLegNames[8] = {
      "leg0", "leg1", "leg2", "leg3", "leg4", "leg5", "leg6", "leg7"};
  for (int i = 0; i < 8; ++i)
  {
    // Alternating pairs: even vs odd, with mild front-to-back stagger.
    const float phase =
        facts.animPhase + (i % 2 == 0 ? 0.0f : kPi) +
        static_cast<float>(i / 2) * 0.28f;
    const float lift = std::sin(phase) * amp;
    const float stride = std::sin(phase + kPi * 0.5f) * amp * 0.4f;
    BoneSkeletonBonePose leg;
    leg.rotationDeg.z = lift;
    leg.rotationDeg.y = stride;
    pose.bones[kLegNames[i]] = leg;
  }

  BoneSkeletonBonePose body0;
  body0.offsetBlocks.y =
      std::sin(facts.animPhase * 2.f) * bodyBob * swingScale;
  pose.bones["body0"] = body0;

  BoneSkeletonBonePose head;
  head.rotationDeg.x = std::sin(facts.animPhase * 0.5f) * 4.f;
  pose.bones["head"] = head;
}

void ApplyFox(BoneSkeletonPose &pose, const CreatureLocomotionFacts &facts,
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
  BoneSkeletonBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  BoneSkeletonBonePose leg1;
  leg1.rotationDeg.x = -legTilt;
  pose.bones["leg1"] = leg1;
  BoneSkeletonBonePose leg2;
  leg2.rotationDeg.x = -legTilt;
  pose.bones["leg2"] = leg2;
  BoneSkeletonBonePose leg3;
  leg3.rotationDeg.x = legTilt;
  pose.bones["leg3"] = leg3;

  BoneSkeletonBonePose tail;
  tail.rotationDeg.x = -35.0f + std::sin(facts.animPhase + kPi * 0.5f) *
                                    tailSwing * 0.45f;
  pose.bones["tail"] = tail;
}

void ApplyHumanoid(BoneSkeletonPose &pose,
                   const CreatureLocomotionFacts &facts,
                   const CreatureDefinition &def)
{
  const float armSwing = def.visual.Animation.armSwingDeg;
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float armTilt = sinP * armSwing * swingScale;
  const float legTilt = sinP * legSwing * swingScale;

  BoneSkeletonBonePose rightArm;
  rightArm.rotationDeg.x = -armTilt;
  pose.bones["rightArm"] = rightArm;
  BoneSkeletonBonePose leftArm;
  leftArm.rotationDeg.x = armTilt;
  pose.bones["leftArm"] = leftArm;
  BoneSkeletonBonePose rightLeg;
  rightLeg.rotationDeg.x = legTilt;
  pose.bones["rightLeg"] = rightLeg;
  BoneSkeletonBonePose leftLeg;
  leftLeg.rotationDeg.x = -legTilt;
  pose.bones["leftLeg"] = leftLeg;

  BoneSkeletonBonePose waist;
  waist.offsetBlocks.y =
      std::sin(facts.animPhase * 2.f) * def.visual.Animation.bodyBobBlocks *
      swingScale;
  pose.bones["waist"] = waist;
}

void ApplyChicken(BoneSkeletonPose &pose,
                  const CreatureLocomotionFacts &facts,
                  const CreatureDefinition &def)
{
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float wingSwing = def.visual.Animation.wingIdleSwingDeg;
  const float swingScale = WalkSwingScale(facts, def);
  const float sinP = std::sin(facts.animPhase);
  const float legTilt = sinP * legSwing * swingScale;

  BoneSkeletonBonePose leg0;
  leg0.rotationDeg.x = legTilt;
  pose.bones["leg0"] = leg0;
  BoneSkeletonBonePose leg1;
  leg1.rotationDeg.x = -legTilt;
  pose.bones["leg1"] = leg1;

  BoneSkeletonBonePose wing0;
  wing0.rotationDeg.z = -wingSwing;
  pose.bones["wing0"] = wing0;
  BoneSkeletonBonePose wing1;
  wing1.rotationDeg.z = wingSwing;
  pose.bones["wing1"] = wing1;

  if (facts.state == LocomotionState::Idle)
  {
    BoneSkeletonBonePose head;
    head.rotationDeg.x = std::sin(facts.animPhase * 3.f) * 12.f;
    pose.bones["head"] = head;
  }
}

void ApplyAerial(BoneSkeletonPose &pose,
                 const CreatureLocomotionFacts &facts,
                 const CreatureDefinition &def)
{
  const float pitch = def.visual.Animation.flyBodyPitchDeg;
  BoneSkeletonBonePose body;
  body.rotationDeg.x = pitch;
  pose.bones["body"] = body;
  const float flap = std::sin(facts.animPhase * 4.f) * 25.f;
  BoneSkeletonBonePose wing0;
  wing0.rotationDeg.z = flap;
  pose.bones["wing0"] = wing0;
  BoneSkeletonBonePose wing1;
  wing1.rotationDeg.z = -flap;
  pose.bones["wing1"] = wing1;
}

void ApplyAquatic(BoneSkeletonPose &pose,
                  const CreatureLocomotionFacts &facts,
                  const CreatureDefinition &def)
{
  const float swingScale = WalkSwingScale(facts, def);
  BoneSkeletonBonePose body;
  body.rotationDeg.y = std::sin(facts.animPhase) * 8.f * swingScale;
  pose.bones["body"] = body;
  BoneSkeletonBonePose tail;
  tail.rotationDeg.y = std::sin(facts.animPhase + kPi * 0.5f) * 15.f * swingScale;
  pose.bones["tail"] = tail;
}

void ApplySerpentine(BoneSkeletonPose &pose,
                     const CreatureLocomotionFacts &facts,
                     const CreatureDefinition &def)
{
  const float swingScale = WalkSwingScale(facts, def);
  BoneSkeletonBonePose body;
  body.rotationDeg.z = std::sin(facts.animPhase) * 6.f * swingScale;
  pose.bones["body"] = body;
}

} // namespace

BoneSkeletonPose
BoneSkeletonPoseEngine::Compute(const CreatureLocomotionFacts &facts,
                               const CreatureDefinition &def, float /*dt*/)
{
  BoneSkeletonPose pose;
  const std::string profile = SkeletalAnimationProfiles::ResolveProfileId(def);

  switch (facts.state)
  {
  case LocomotionState::Walk:
  case LocomotionState::Run:
    if (profile == "fox")
    {
      ApplyFox(pose, facts, def);
    }
    else if (profile == "arachnid")
    {
      ApplyArachnid(pose, facts, def);
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
    else if (profile == "arachnid")
    {
      ApplyArachnid(pose, facts, def);
    }
    else if (profile == "chicken")
    {
      ApplyChicken(pose, facts, def);
    }
    else if (profile == "humanoid")
    {
      BoneSkeletonBonePose head;
      head.rotationDeg.x = std::sin(facts.animPhase * 0.3f) * 8.f;
      pose.bones["head"] = head;
    }
  }
  return pose;
}

} // namespace cutum
