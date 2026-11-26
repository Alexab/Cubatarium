#ifndef BONE_SKELETON_CUBE_MESH_BUILDER_H
#define BONE_SKELETON_CUBE_MESH_BUILDER_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <memory>

namespace cutum
{

class StlSourceStorage;

struct SkeletonMeshBuildStorage
{
  size_t controlBytes{}, emptyIndexBytes{}, meshBytes{}, requestedBytes{};
  size_t bones{}, cubes{}, explicitScratchBytes{};
  bool supported{};
};

struct SkeletonMeshBuildSnapshot
{
  size_t completed{}, unsupported{};
  SkeletonMeshBuildStorage largest;
};

void MeasureSkeletonMeshes(const CreatureBoneSkeletonMeshAsset &asset,
                           StlSourceStorage &out) noexcept;
SkeletonMeshBuildSnapshot GetSkeletonMeshBuildSnapshot();

class BoneSkeletonCubeMeshBuilder
{
public:
  static BoneSkeletonCubeMeshCpu BuildCubeMesh(const BoneSkeletonCubeDef &cube,
                                          const glm::vec3 &bonePivotBlocks,
                                          const glm::ivec2 &textureSize,
                                          bool mirrorUv);
  static CreatureBoneSkeletonMeshAsset
  BuildMeshAsset(const CreatureBoneSkeletonGeometry &geometry);
  static std::shared_ptr<const CreatureBoneSkeletonMeshAsset>
  BuildSharedMeshAsset(CreatureBoneSkeletonGeometry &&geometry,
                        SkeletonMeshBuildStorage *storage = nullptr);
};

} // namespace cutum

#endif
