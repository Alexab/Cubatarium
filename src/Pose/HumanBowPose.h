#pragma once
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/BoneSkeleton/CreatureAttachmentBounds.h"
#include "Creatures/Visual/CreatureEquipmentFit.h"
#include "Items/ItemPresentationMath.h"
#include "Pose/HumanArmPose.h"

namespace cutum
{
/// Contact correction for the versioned human rig only. No creature buffers or
/// locomotion state are changed; targets use the actual drawable palm bounds.
class UHumanBowPose
{

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
    if (!UHumanArmPose::ReachSphere(mesh, rig, corrected, false, centers[0],
                                    radii[0]) ||
        !UHumanArmPose::ReachSphere(mesh, rig, corrected, true, centers[1],
                                    radii[1]))
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
    if (!UHumanArmPose::SolveArm(mesh, rig, corrected, false, target, weight))
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
    if (!UHumanArmPose::SolveArm(mesh, rig, corrected, true, support, weight))
      return false;
    pose = std::move(corrected);
    return true;
  }
};
} // namespace cutum
