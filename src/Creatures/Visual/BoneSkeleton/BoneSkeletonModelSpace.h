#ifndef BONE_SKELETON_MODEL_SPACE_H
#define BONE_SKELETON_MODEL_SPACE_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

constexpr float kBoneSkeletonUnitsPerBlock = 16.f;

/// Skeletal geo stores positions/pivots in 1/16-block units (model space, Y-up).
inline glm::vec3 BoneSkeletonUnitsToBlocks(const glm::vec3 &geoUnits)
{
  return geoUnits / kBoneSkeletonUnitsPerBlock;
}

/// Minimum thickness for skeletal cubes with a zero axis (wings, legs, stinger).
inline float BoneSkeletonThinAxisBlocks()
{
  return 1.f / kBoneSkeletonUnitsPerBlock;
}

/// Which faces to emit when a cube has zero thickness on one axis.
inline bool BoneSkeletonCubeFaceVisible(int faceIndex, const glm::vec3 &sizeBlocks)
{
  if (sizeBlocks.x <= 0.f)
  {
    return faceIndex == 1 || faceIndex == 3;
  }
  if (sizeBlocks.y <= 0.f)
  {
    return faceIndex == 4 || faceIndex == 5;
  }
  if (sizeBlocks.z <= 0.f)
  {
    return faceIndex == 0 || faceIndex == 2;
  }
  return true;
}

/// visible_bounds_* in geo.json are already in blocks (not 1/16 units).
inline glm::vec3 BoneSkeletonVisibleBoundsOffsetBlocks(const glm::vec3 &raw)
{
  return raw;
}

/// Entity forward −Z → Cubatarium +Z. Applied on the assembled skeleton at draw time.
inline glm::mat4 BoneSkeletonEntityConventionMatrix()
{
  return glm::scale(glm::mat4(1.f), glm::vec3(1.f, 1.f, -1.f));
}

/// Center and scale a skeletal model for inventory preview (orbit is view-only).
inline glm::mat4 BoneSkeletonPreviewRootMatrix(const CreatureBoneSkeletonGeometry &geometry,
                                          float targetSpanBlocks)
{
  const float span =
      std::max(geometry.visibleBoundsWidthBlocks, geometry.visibleBoundsHeightBlocks);
  const float scale = targetSpanBlocks / std::max(span, 0.25f);
  glm::mat4 m = glm::scale(glm::mat4(1.f), glm::vec3(scale));
  m = m * BoneSkeletonEntityConventionMatrix();
  m = glm::translate(m, -geometry.visibleBoundsOffsetBlocks);
  return m;
}

/// `bind_pose_rotation` in geo is authored for upstream entity space; map to model Y-up.
inline glm::vec3 BoneSkeletonBindPoseRotationDeg(const glm::vec3 &geoDeg)
{
  return glm::vec3(-geoDeg.x, -geoDeg.y, geoDeg.z);
}

/// Skeletal bone/cube rotation order: X, then Y, then Z (degrees).
inline glm::mat4 BoneSkeletonEulerDegToMat(const glm::vec3 &eulerDeg)
{
  glm::mat4 m(1.f);
  if (eulerDeg.z != 0.f)
  {
    m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0.f, 0.f, 1.f));
  }
  if (eulerDeg.y != 0.f)
  {
    m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0.f, 1.f, 0.f));
  }
  if (eulerDeg.x != 0.f)
  {
    m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1.f, 0.f, 0.f));
  }
  return m;
}

} // namespace cutum

#endif
