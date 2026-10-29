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
  if (facts.horizontalSpeed < 0.05f)
  {
    return 0.0f;
  }
  float swingScale =
      std::clamp(facts.horizontalSpeed / walkSpeed, 0.35f, 1.5f);
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
  // Z lifts in the vertical plane; Y adds stride. Right-side bones are mirrored,
  // so Z/Y signs flip for odd indices or same-phase groups look synchronized.
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float bodyBob = def.visual.Animation.bodyBobBlocks;
  const float speed = std::max(facts.horizontalSpeed, 0.0f);
  if (speed < 0.05f)
  {
    BoneSkeletonBonePose head;
    head.rotationDeg.x = std::sin(facts.animPhase * 0.35f) * 3.f;
    pose.bones["head"] = head;
    return;
  }

  const float swingScale =
      std::clamp(speed / std::max(def.locomotion.walkSpeed, 0.01f), 0.35f, 1.5f);
  const float amp = legSwing * swingScale;

  static constexpr const char *kLegNames[8] = {
      "leg0", "leg1", "leg2", "leg3", "leg4", "leg5", "leg6", "leg7"};
  // Tetrapod alternating gait (Minecraft-style): A={0,3,4,7} vs B={1,2,5,6}.
  static constexpr int kPhaseGroup[8] = {0, 1, 1, 0, 0, 1, 1, 0};
  for (int i = 0; i < 8; ++i)
  {
    const float phase =
        facts.animPhase + (kPhaseGroup[i] != 0 ? kPi : 0.0f);
    const float lift = std::sin(phase) * amp;
    const float stride = std::cos(phase) * amp * 0.55f;
    // even = left (−X), odd = right (+X, mirrored): flip so lift reads as up.
    const float side = (i % 2 == 0) ? 1.0f : -1.0f;
    // Rest fan: legs splay outward from body (MC/Luanti look), then gait on top.
    // Pair index 0..3 along body; base yaw ~±35° plus small per-pair spread.
    const float pair = static_cast<float>(i / 2);
    const float restYaw = side * (32.0f + pair * 8.0f);
    BoneSkeletonBonePose leg;
    leg.rotationDeg.y = restYaw + stride * side;
    leg.rotationDeg.z = lift * side;
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

void ApplyRabbit(BoneSkeletonPose &pose,
                 const CreatureLocomotionFacts &facts,
                 const CreatureDefinition &def)
{
  // Hop gait: both haunches/rear feet kick together, front legs counter.
  const float swingScale = WalkSwingScale(facts, def);
  const float legSwing = def.visual.Animation.legSwingDeg;
  const float hop = std::sin(facts.animPhase);
  const float rear = hop * legSwing * 1.6f * swingScale;
  const float front = -hop * legSwing * 1.1f * swingScale;
  for (const char *name : {"haunchLeft", "haunchRight", "rearFootLeft",
                           "rearFootRight"})
  {
    BoneSkeletonBonePose bone;
    bone.rotationDeg.x = rear;
    pose.bones[name] = bone;
  }
  for (const char *name : {"frontLegLeft", "frontLegRight"})
  {
    BoneSkeletonBonePose bone;
    bone.rotationDeg.x = front;
    pose.bones[name] = bone;
  }
  BoneSkeletonBonePose body;
  body.offsetBlocks.y = std::max(0.0f, hop) * 0.12f * swingScale;
  body.rotationDeg.x = -hop * 6.f * swingScale;
  pose.bones["body"] = body;
  BoneSkeletonBonePose ear;
  ear.rotationDeg.x = std::sin(facts.animPhase * 2.f) * 8.f * swingScale;
  pose.bones["earLeft"] = ear;
  pose.bones["earRight"] = ear;
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
  // Bee-style rigs name the wing bones differently.
  pose.bones["rightwing_bone"] = wing0;
  pose.bones["leftwing_bone"] = wing1;
}

void ApplyAquatic(BoneSkeletonPose &pose,
                  const CreatureLocomotionFacts &facts,
                  const CreatureDefinition &def)
{
  // Fish keep a gentle sway even when drifting (Idle/Tread in water).
  const float swingScale = std::max(WalkSwingScale(facts, def), 0.35f);
  BoneSkeletonBonePose body;
  body.rotationDeg.y = std::sin(facts.animPhase) * 8.f * swingScale;
  pose.bones["body"] = body;
  BoneSkeletonBonePose tail;
  tail.rotationDeg.y = std::sin(facts.animPhase + kPi * 0.5f) * 15.f * swingScale;
  pose.bones["tail"] = tail;
  // Segmented fish (trout): rear half and tail fin trail the front.
  pose.bones["body_back"] = tail;
  BoneSkeletonBonePose tailFin;
  tailFin.rotationDeg.y =
      std::sin(facts.animPhase + kPi * 0.9f) * 22.f * swingScale;
  pose.bones["tailfin"] = tailFin;
  pose.bones["tail_fin"] = tailFin;
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

// Pose for a locomotion cycle of the given profile (legs/wings/fins driven by
// facts.animPhase).
void ApplyProfileCycle(BoneSkeletonPose &pose, const std::string &profile,
                       const CreatureLocomotionFacts &facts,
                       const CreatureDefinition &def)
{
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
  else if (profile == "rabbit")
  {
    ApplyRabbit(pose, facts, def);
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
}

bool IsGroundProfile(const std::string &profile)
{
  return profile != "aerial" && profile != "aquatic" &&
         profile != "serpentine";
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
  case LocomotionState::Swim:
  case LocomotionState::Tread:
  case LocomotionState::Slither:
  case LocomotionState::Hover:
  case LocomotionState::Glide:
    ApplyProfileCycle(pose, profile, facts, def);
    break;
  case LocomotionState::Fly:
    ApplyAerial(pose, facts, def);
    break;
  case LocomotionState::Jump:
  case LocomotionState::Fall:
  case LocomotionState::Crouch:
  {
    // Airborne/crouched ground mobs: hold a stride mid-cycle instead of the
    // bind pose (legs splayed while falling, tucked while crouching).
    if (IsGroundProfile(profile))
    {
      CreatureLocomotionFacts held = facts;
      held.horizontalSpeed = std::max(def.locomotion.walkSpeed, 0.05f);
      held.animPhase = facts.state == LocomotionState::Crouch ? 0.0f : 1.2f;
      held.state = LocomotionState::Walk;
      ApplyProfileCycle(pose, profile, held, def);
    }
    break;
  }
  case LocomotionState::Idle:
  default:
    if (profile == "fox" || profile == "arachnid" || profile == "chicken" ||
        profile == "aquatic")
    {
      ApplyProfileCycle(pose, profile, facts, def);
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
