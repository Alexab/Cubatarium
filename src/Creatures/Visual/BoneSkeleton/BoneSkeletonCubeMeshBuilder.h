#ifndef BONE_SKELETON_CUBE_MESH_BUILDER_H
#define BONE_SKELETON_CUBE_MESH_BUILDER_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"

namespace cutum
{

class BoneSkeletonCubeMeshBuilder
{
public:
  static BoneSkeletonCubeMeshCpu BuildCubeMesh(const BoneSkeletonCubeDef &cube,
                                          const glm::vec3 &bonePivotBlocks,
                                          const glm::ivec2 &textureSize,
                                          bool mirrorUv);
  static CreatureBoneSkeletonMeshAsset
  BuildMeshAsset(const CreatureBoneSkeletonGeometry &geometry);
};

} // namespace cutum

#endif
