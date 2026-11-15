#pragma once
#include "Pose/HumanArmPose.h"
#include <array>
#include <limits>

namespace cutum
{
/// Presentation-only upper-body reach. Feet, collision proxy and carried body
/// are untouched. The torso bends/reaches before either arm is solved.
class UHumanCarryPose
{
public:
  static bool Apply(const CreatureBoneSkeletonMeshAsset &mesh,
                    const BoneSkeletonHierarchy &rig, BoneSkeletonPose &pose,
                    const std::array<glm::vec3, 2> &targets, int hands)
  {
    if (mesh.geometry.humanRigVersion != 2 || hands < 1 || hands > 2)
      return false;
    const auto &index = mesh.geometry.boneIndexByName;
    if (!index.count("spine") || !index.count("head"))
      return false;
    const auto base = pose;
    auto best = base;
    float bestScore = std::numeric_limits<float>::max();
    bool reached = false;
    // A small shoulder rise/compression is an upper-body pose, not a change
    // in creature height. Prefer zero shift and the least necessary bend.
    for (float shift : {0.f, .1f, .15f, -.1f})
    {
      for (int angle = 0; angle <= 105; angle += 5)
      {
        auto candidate = base;
        auto &spine = candidate.bones["spine"];
        const auto lean =
            glm::angleAxis(glm::radians(-float(angle)), glm::vec3(1, 0, 0));
        spine.rotation = lean;
        spine.quaternionRotation = true;
        spine.offsetBlocks.y += shift;
        // Lumbar flexion translates the shoulder girdle slightly forwards as
        // it bends. The mesh still overlaps the pelvis; feet and limb lengths
        // remain fixed. No physical body or cargo position is adjusted.
        spine.offsetBlocks.z -= .14f * std::sin(glm::radians(float(angle)));
        auto &head = candidate.bones["head"];
        const auto previousHead =
            head.quaternionRotation
                ? head.rotation
                : glm::quat_cast(BoneSkeletonEulerDegToMat(head.rotationDeg));
        head.rotation = glm::normalize(glm::inverse(lean) * previousHead);
        head.quaternionRotation = true;
        float error = 0;
        for (int hand = 0; hand < hands; ++hand)
        {
          glm::vec3 center;
          float radius = 0, minimum = 0;
          if (!UHumanArmPose::ReachSphere(mesh, rig, candidate, hand != 0,
                                          center, radius, &minimum))
            return false;
          const float distance = glm::distance(center, targets[size_t(hand)]);
          const float gap = std::max(
              {0.f, distance - radius + .003f, minimum + .003f - distance});
          error += gap * gap;
        }
        const float score =
            error + float(angle * angle) * .00000001f + shift * shift * .0001f;
        if (score < bestScore)
        {
          bestScore = score;
          best = std::move(candidate);
        }
        if (error < .0000001f)
        {
          reached = true;
          break;
        }
      }
      if (reached)
        break;
    }
    for (int hand = 0; hand < hands; ++hand)
      if (!UHumanArmPose::SolveArm(mesh, rig, best, hand != 0,
                                   targets[size_t(hand)], 1.f))
        return false;
    pose = std::move(best);
    return true;
  }
  static void Blend(BoneSkeletonPose &pose, const BoneSkeletonPose &carry,
                    float weight)
  {
    for (const char *name : {"spine", "head", "rightArm", "rightForearm",
                             "rightHand", "leftArm", "leftForearm", "leftHand"})
    {
      const auto found = carry.bones.find(name);
      if (found == carry.bones.end())
        continue;
      auto &bone = pose.bones[name];
      const auto &target = found->second;
      const auto a =
          bone.quaternionRotation
              ? bone.rotation
              : glm::quat_cast(BoneSkeletonEulerDegToMat(bone.rotationDeg));
      const auto b =
          target.quaternionRotation
              ? target.rotation
              : glm::quat_cast(BoneSkeletonEulerDegToMat(target.rotationDeg));
      bone.rotation = glm::normalize(glm::slerp(a, b, weight));
      bone.quaternionRotation = true;
      bone.offsetBlocks =
          glm::mix(bone.offsetBlocks, target.offsetBlocks, weight);
    }
  }
};
} // namespace cutum
