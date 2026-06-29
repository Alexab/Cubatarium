#ifndef SKELETAL_CUBE_MESH_BUILDER_H
#define SKELETAL_CUBE_MESH_BUILDER_H

#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"

namespace cutum
{

class SkeletalCubeMeshBuilder
{
public:
  static SkeletalCubeMeshCpu BuildCubeMesh(const SkeletalCubeDef &cube,
                                          const glm::vec3 &bonePivotBlocks,
                                          const glm::ivec2 &textureSize,
                                          bool mirrorUv);
  static CreatureSkeletalMeshAsset
  BuildMeshAsset(const CreatureSkeletalGeometry &geometry);
};

} // namespace cutum

#endif
