#pragma once
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/CreatureAttachmentBounds.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Items/ItemPresentationMath.h"

namespace cutum
{
/// Shared two-bone solve with actual mesh-derived palm contacts and unchanged
/// limb lengths. Targets outside the reach sphere are clamped, never stretched.
class UHumanArmPose
{
public:
  static bool ReachSphere(const CreatureBoneSkeletonMeshAsset &mesh,
                          const BoneSkeletonHierarchy &rig,
                          const BoneSkeletonPose &pose, bool left,
                          glm::vec3 &center, float &radius,
                          float *minimumRadius = nullptr)
  {
    const auto &g = mesh.geometry;
    const std::string side = left ? "left" : "right";
    const auto upper = g.boneIndexByName.find(side + "Arm");
    const auto lower = g.boneIndexByName.find(side + "Forearm");
    const auto item = g.boneIndexByName.find(side + "Item");
    glm::vec3 low, high;
    if (upper == g.boneIndexByName.end() || lower == g.boneIndexByName.end() ||
        item == g.boneIndexByName.end() ||
        !ResolveCreatureAttachmentBounds(mesh, rig, side + "Item", low, high))
      return false;
    const BoneSkeletonPose rest;
    const auto a = rig.ComputeBoneMatrix(upper->second, rest);
    const auto b = rig.ComputeBoneMatrix(lower->second, rest);
    const auto palm = rig.ComputeBoneMatrix(item->second, rest) *
                      glm::vec4(CreaturePalmGripPosition(low, high), 1);
    radius = glm::distance(glm::vec3(a[3]), glm::vec3(b[3])) +
             glm::distance(glm::vec3(b[3]), glm::vec3(palm)) - .001f;
    if (minimumRadius)
      *minimumRadius =
          std::abs(glm::distance(glm::vec3(a[3]), glm::vec3(b[3])) -
                   glm::distance(glm::vec3(b[3]), glm::vec3(palm))) +
          .001f;
    center = glm::vec3(rig.ComputeBoneMatrix(upper->second, pose)[3]);
    return radius > .001f;
  }

  static bool SolveArm(const CreatureBoneSkeletonMeshAsset &mesh,
                       const BoneSkeletonHierarchy &rig, BoneSkeletonPose &pose,
                       bool left, glm::vec3 target, float weight)
  {
    const auto &g = mesh.geometry;
    const std::string side = left ? "left" : "right";
    const auto upper = g.boneIndexByName.find(side + "Arm");
    const auto lower = g.boneIndexByName.find(side + "Forearm");
    const auto item = g.boneIndexByName.find(side + "Item");
    if (upper == g.boneIndexByName.end() || lower == g.boneIndexByName.end() ||
        item == g.boneIndexByName.end())
      return false;
    const auto &upperBone = g.bones[upper->second];
    const auto parent = g.boneIndexByName.find(upperBone.parent);
    if (parent == g.boneIndexByName.end())
      return false;
    glm::vec3 low, high;
    if (!ResolveCreatureAttachmentBounds(mesh, rig, side + "Item", low, high))
      return false;
    const auto palm = CreaturePalmGripPosition(low, high);
    const BoneSkeletonPose rest;
    const auto restUpper = rig.ComputeBoneMatrix(upper->second, rest);
    const auto restLower = rig.ComputeBoneMatrix(lower->second, rest);
    const auto restPalm =
        rig.ComputeBoneMatrix(item->second, rest) * glm::vec4(palm, 1);
    const auto first = glm::vec3(glm::inverse(restUpper) * restLower[3]);
    const auto second = glm::vec3(glm::inverse(restLower) * restPalm);
    const float a = glm::length(first), b = glm::length(second);
    if (a < .001f || b < .001f)
      return false;
    const auto parentFrame = rig.ComputeBoneMatrix(parent->second, pose);
    const auto shoulder =
        parentFrame *
        glm::translate(glm::mat4(1), upperBone.pivotBlocks -
                                         g.bones[parent->second].pivotBlocks +
                                         pose.bones[side + "Arm"].offsetBlocks);
    auto delta = glm::vec3(glm::inverse(shoulder) * glm::vec4(target, 1));
    const float distance = glm::clamp(glm::length(delta),
                                      std::abs(a - b) + .0001f, a + b - .0001f);
    const auto axis = glm::length(delta) > .00001f ? glm::normalize(delta)
                                                   : glm::vec3(0, 0, -1);
    auto pole = glm::vec3(left ? 1.f : -1.f, -.35f, 0);
    pole -= axis * glm::dot(pole, axis);
    if (glm::length(pole) < .00001f)
      pole = glm::cross(axis, glm::vec3(0, 0, 1));
    pole = glm::normalize(pole);
    const float along =
        (a * a - b * b + distance * distance) / (2.f * distance);
    const auto elbow =
        axis * along + pole * std::sqrt(std::max(0.f, a * a - along * along));
    const auto upperRotation = ItemTurnBetween(first, elbow);
    const auto lowerRotation = glm::inverse(upperRotation) *
                               ItemTurnBetween(second, axis * distance - elbow);
    auto set = [&](const std::string &name, glm::quat targetRotation)
    {
      auto &bone = pose.bones[name];
      const auto previous =
          bone.quaternionRotation
              ? bone.rotation
              : glm::quat_cast(BoneSkeletonEulerDegToMat(bone.rotationDeg));
      bone.rotation =
          glm::normalize(glm::slerp(previous, targetRotation, weight));
      bone.quaternionRotation = true;
    };
    set(side + "Arm", upperRotation);
    set(side + "Forearm", lowerRotation);
    set(side + "Hand", glm::quat(1, 0, 0, 0));
    return true;
  }
};
} // namespace cutum
