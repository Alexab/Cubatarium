#ifndef BONE_SKELETON_HIERARCHY_H
#define BONE_SKELETON_HIERARCHY_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class BoneSkeletonHierarchy
{
public:
  explicit BoneSkeletonHierarchy(const CreatureBoneSkeletonGeometry &geometry);

  glm::mat4 ComputeBoneMatrix(size_t boneIndex,
                              const BoneSkeletonPose &pose) const;

  const CreatureBoneSkeletonGeometry &GetGeometry() const { return Geometry; }

private:
  /// Parent animation chain only (no bind_pose_rotation).
  glm::mat4 ComputeBonePoseChain(size_t boneIndex,
                                 const BoneSkeletonPose &pose) const;

  CreatureBoneSkeletonGeometry Geometry;
  std::vector<int> parentIndices;
};

} // namespace cutum

#endif
