#pragma once
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/CreatureAttachmentBounds.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Items/ItemPresentationMath.h"

namespace cutum
{
/// Contact correction for the versioned human rig only. No creature buffers or
/// locomotion state are changed; targets use the actual drawable palm bounds.
class UHumanBowPose
{
  static bool ReachSphere(const CreatureBoneSkeletonMeshAsset &mesh,
                          const BoneSkeletonHierarchy &rig,
                          const BoneSkeletonPose &pose, bool left,
                          glm::vec3 &center, float &radius)
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

public:
  static bool Apply(const CreatureBoneSkeletonMeshAsset &mesh,
                    const BoneSkeletonHierarchy &rig, BoneSkeletonPose &pose,
                    const ItemRenderAsset &asset,
                    const ItemVisualInputs &inputs, float scale, float weight)
  {
    if (mesh.geometry.humanRigVersion != 2 || !ItemHasSupportGrip(asset) ||
        weight <= 0)
      return false;
    const auto &indices = mesh.geometry.boneIndexByName;
    for (const char *name : {"head", "rightArm", "leftArm", "rightItem"})
      if (indices.find(name) == indices.end())
        return false;
    auto corrected = pose;
    const auto head = rig.ComputeBoneMatrix(indices.at("head"), corrected);
    auto fullInputs = inputs;
    fullInputs.BowCharge = 1;
    const auto fullNodes = ResolveItemNodeTransforms(
        asset, EvaluateItemVisualState(asset, fullInputs));
    const auto fullOffset =
        glm::vec3(ItemBowModelMatrix(asset, glm::vec3(0), head, scale) *
                  fullNodes[asset.Grip.SupportGripIndex][3]);
    // Seed the bow from the rendered face, including crouch/torso lean.
    // The holding hand stays steady while the string hand travels to the face.
    auto target = glm::vec3(head * glm::vec4(0, .12f, -.34f, 1)) - fullOffset;
    const auto state = EvaluateItemVisualState(asset, inputs);
    const auto transforms = ResolveItemNodeTransforms(asset, state);
    const auto supportOffset =
        glm::vec3(ItemBowModelMatrix(asset, glm::vec3(0), head, scale) *
                  transforms[asset.Grip.SupportGripIndex][3]);
    glm::vec3 centers[2];
    float radii[2];
    if (!ReachSphere(mesh, rig, corrected, false, centers[0], radii[0]) ||
        !ReachSphere(mesh, rig, corrected, true, centers[1], radii[1]))
      return false;
    centers[1] -= supportOffset;
    // Move the whole bow into the intersection of both arms' reachable
    // volumes. Its orientation and the animated string separation stay intact.
    for (int iteration = 0; iteration < 24; ++iteration)
      for (int hand = 0; hand < 2; ++hand)
      {
        const auto delta = target - centers[hand];
        const float distance = glm::length(delta);
        if (distance > radii[hand])
          target = centers[hand] + delta * (radii[hand] / distance);
      }
    if (!SolveArm(mesh, rig, corrected, false, target, weight))
      return false;
    glm::vec3 low, high;
    if (!ResolveCreatureAttachmentBounds(mesh, rig, "rightItem", low, high))
      return false;
    const auto palm =
        glm::vec3(rig.ComputeBoneMatrix(indices.at("rightItem"), corrected) *
                  glm::vec4(CreaturePalmGripPosition(low, high), 1));
    const auto model = ItemBowModelMatrix(asset, palm, head, scale);
    const auto support =
        glm::vec3(model * transforms[asset.Grip.SupportGripIndex][3]);
    if (!SolveArm(mesh, rig, corrected, true, support, weight))
      return false;
    pose = std::move(corrected);
    return true;
  }
};
} // namespace cutum
