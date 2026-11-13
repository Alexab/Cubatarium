#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"

#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace cutum
{

namespace
{

// restLocalMatrix carries (meshCenter - pivot). Bind uses T(p)*R; pose uses T(p)*R
// (rotation around pivot because restLocal already offsets mesh from pivot).
glm::mat4 BoneLocalMatrix(const glm::vec3 &pivotBlocks,
                          const glm::vec3 &bindRotationDeg,
                          const glm::mat4 &poseRotation,
                          const glm::vec3 &animOffset)
{
  glm::mat4 local =
      glm::translate(glm::mat4(1.f), pivotBlocks + animOffset);
  if (glm::length(bindRotationDeg) > 0.001f)
  {
    local = local * BoneSkeletonEulerDegToMat(bindRotationDeg);
  }
  local = local * poseRotation;
  return local;
}

glm::mat4 BonePoseOnlyMatrix(const glm::vec3 &pivotBlocks,
                             const glm::mat4 &rotation,
                             const glm::vec3 &animOffset)
{


  glm::mat4 pose(1.f);
  if (glm::length(animOffset) > 0.001f)
  {
    pose = glm::translate(pose, animOffset);
  }
  {
    pose = glm::translate(pose, pivotBlocks);
    pose = pose * rotation;
    pose = glm::translate(pose, -pivotBlocks);
  }
  return pose;
}

bool AncestorHasBindPoseRotation(
    const CreatureBoneSkeletonGeometry &geometry,
    const std::vector<int> &parentIndices, size_t boneIndex)
{
  int idx = static_cast<int>(boneIndex);
  while (idx >= 0)
  {
    const BoneSkeletonBoneDef &bone = geometry.bones[static_cast<size_t>(idx)];
    if (glm::length(bone.bindPoseRotationDeg) > 0.001f)
    {
      return true;
    }
    idx = parentIndices[static_cast<size_t>(idx)];
  }
  return false;
}

bool UsesAbsolutePivotPlacement(
    const CreatureBoneSkeletonGeometry &geometry,
    const std::vector<int> &parentIndices, size_t boneIndex)
{
  const int parentIdx = parentIndices[boneIndex];
  if (parentIdx < 0)
  {
    return true;
  }
  if (glm::length(geometry.bones[boneIndex].bindPoseRotationDeg) > 0.001f)
  {
    return true;
  }
  return AncestorHasBindPoseRotation(geometry, parentIndices,
                                     static_cast<size_t>(parentIdx));
}

} // namespace

BoneSkeletonHierarchy::BoneSkeletonHierarchy(
    const CreatureBoneSkeletonGeometry &geometry)
    : Geometry(geometry)
{
  parentIndices.resize(geometry.bones.size(), -1);
  for (size_t i = 0; i < geometry.bones.size(); ++i)
  {
    const std::string &parentName = geometry.bones[i].parent;
    if (parentName.empty())
    {
      continue;
    }
    const auto it = geometry.boneIndexByName.find(parentName);
    if (it != geometry.boneIndexByName.end())
    {
      parentIndices[i] = static_cast<int>(it->second);
    }
  }
}

glm::mat4 BoneSkeletonHierarchy::ComputeBoneMatrix(
    size_t boneIndex, const BoneSkeletonPose &pose) const
{
  if (boneIndex >= Geometry.bones.size())
  {
    return glm::mat4(1.f);
  }

  const BoneSkeletonBoneDef &bone = Geometry.bones[boneIndex];
  const int parentIdx = parentIndices[boneIndex];

  const glm::vec3 bindRot =
      BoneSkeletonBindPoseRotationDeg(bone.bindPoseRotationDeg) + bone.boneRotationDeg;
  glm::mat4 poseRot{1.f};
  glm::vec3 animOffset{0.f};
  if (const auto it = pose.bones.find(bone.name); it != pose.bones.end())
  {
    poseRot = it->second.quaternionRotation ? glm::mat4_cast(it->second.rotation) : BoneSkeletonEulerDegToMat(it->second.rotationDeg);
    animOffset = it->second.offsetBlocks;
  }

  if (UsesAbsolutePivotPlacement(Geometry, parentIndices, boneIndex))
  {
    glm::mat4 poseChain(1.f);
    if (parentIdx >= 0)
    {
      poseChain = ComputeBonePoseChain(static_cast<size_t>(parentIdx), pose);
    }
    return poseChain *
           BoneLocalMatrix(bone.pivotBlocks, bindRot, poseRot, animOffset);
  }

  const glm::mat4 parentMat =
      ComputeBoneMatrix(static_cast<size_t>(parentIdx), pose);
  const glm::vec3 delta =
      bone.pivotBlocks - Geometry.bones[static_cast<size_t>(parentIdx)].pivotBlocks;

  glm::mat4 local = glm::translate(glm::mat4(1.f), delta + animOffset);
  if (glm::length(bindRot) > 0.001f)
  {
    local = local * BoneSkeletonEulerDegToMat(bindRot);
  }
  local = local * poseRot;
  return parentMat * local;
}

glm::mat4 BoneSkeletonHierarchy::ComputeBonePoseChain(
    size_t boneIndex, const BoneSkeletonPose &pose) const
{
  const BoneSkeletonBoneDef &bone = Geometry.bones[boneIndex];
  glm::mat4 chain(1.f);
  const int parentIdx = parentIndices[boneIndex];
  if (parentIdx >= 0)
  {
    chain = ComputeBonePoseChain(static_cast<size_t>(parentIdx), pose);
  }

  glm::mat4 animRot{1.f};
  glm::vec3 animOffset{0.f};
  if (const auto it = pose.bones.find(bone.name); it != pose.bones.end())
  {
    animRot = it->second.quaternionRotation ? glm::mat4_cast(it->second.rotation) : BoneSkeletonEulerDegToMat(it->second.rotationDeg);
    animOffset = it->second.offsetBlocks;
  }

  return chain * BonePoseOnlyMatrix(bone.pivotBlocks, animRot, animOffset);
}

} // namespace cutum
