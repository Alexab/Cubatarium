#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"

#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

namespace
{

// restLocalMatrix carries (meshCenter - pivot). Bind uses T(p)*R; pose uses T(p)*R
// (rotation around pivot because restLocal already offsets mesh from pivot).
glm::mat4 BoneLocalMatrix(const glm::vec3 &pivotBlocks,
                          const glm::vec3 &bindRotationDeg,
                          const glm::vec3 &poseRotationDeg,
                          const glm::vec3 &animOffset)
{
  glm::mat4 local =
      glm::translate(glm::mat4(1.f), pivotBlocks + animOffset);
  if (glm::length(bindRotationDeg) > 0.001f)
  {
    local = local * BoneSkeletonEulerDegToMat(bindRotationDeg);
  }
  if (glm::length(poseRotationDeg) > 0.001f)
  {
    local = local * BoneSkeletonEulerDegToMat(poseRotationDeg);
  }
  return local;
}

glm::mat4 BonePoseOnlyMatrix(const glm::vec3 &pivotBlocks,
                             const glm::vec3 &rotationDeg,
                             const glm::vec3 &animOffset)
{
  if (glm::length(rotationDeg) <= 0.001f && glm::length(animOffset) <= 0.001f)
  {
    return glm::mat4(1.f);
  }

  glm::mat4 pose(1.f);
  if (glm::length(animOffset) > 0.001f)
  {
    pose = glm::translate(pose, animOffset);
  }
  if (glm::length(rotationDeg) > 0.001f)
  {
    pose = glm::translate(pose, pivotBlocks);
    pose = pose * BoneSkeletonEulerDegToMat(rotationDeg);
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
  glm::vec3 poseRot{0.f};
  glm::vec3 animOffset{0.f};
  if (const auto it = pose.bones.find(bone.name); it != pose.bones.end())
  {
    poseRot = it->second.rotationDeg;
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
  if (glm::length(poseRot) > 0.001f)
  {
    local = local * BoneSkeletonEulerDegToMat(poseRot);
  }
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

  glm::vec3 animRot{0.f};
  glm::vec3 animOffset{0.f};
  if (const auto it = pose.bones.find(bone.name); it != pose.bones.end())
  {
    animRot = it->second.rotationDeg;
    animOffset = it->second.offsetBlocks;
  }

  return chain * BonePoseOnlyMatrix(bone.pivotBlocks, animRot, animOffset);
}

} // namespace cutum
