#ifndef CREATURE_BONE_HIERARCHY_H
#define CREATURE_BONE_HIERARCHY_H

#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class CreatureBoneHierarchy
{
public:
  explicit CreatureBoneHierarchy(const CreatureSkeletalGeometry &geometry);

  glm::mat4 ComputeBoneMatrix(size_t boneIndex,
                              const SkeletalCreaturePose &pose) const;

  const CreatureSkeletalGeometry &GetGeometry() const { return Geometry; }

private:
  /// Parent animation chain only (no bind_pose_rotation).
  glm::mat4 ComputeBonePoseChain(size_t boneIndex,
                                 const SkeletalCreaturePose &pose) const;

  CreatureSkeletalGeometry Geometry;
  std::vector<int> parentIndices;
};

} // namespace cutum

#endif
