#pragma once
#include "Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.h"
#include <limits>

namespace cutum
{
/// Mesh-derived local bounds for a drawable bone or its empty attachment node.
/// Shared by equipment and contact tests so neither can substitute the pivot.
inline bool
ResolveCreatureAttachmentBounds(const CreatureBoneSkeletonMeshAsset &asset,
                                const BoneSkeletonHierarchy &hierarchy,
                                const std::string &name, glm::vec3 &low,
                                glm::vec3 &high)
{
  const auto anchor = asset.geometry.boneIndexByName.find(name);
  if (anchor == asset.geometry.boneIndexByName.end())
    return false;
  size_t source = anchor->second;
  while (asset.boneMeshes[source].cubes.empty())
  {
    const auto parent = asset.geometry.boneIndexByName.find(
        asset.geometry.bones[source].parent);
    if (parent == asset.geometry.boneIndexByName.end())
      return false;
    source = parent->second;
  }
  const BoneSkeletonPose rest;
  const auto intoAnchor =
      glm::inverse(hierarchy.ComputeBoneMatrix(anchor->second, rest)) *
      hierarchy.ComputeBoneMatrix(source, rest);
  low = glm::vec3(std::numeric_limits<float>::max());
  high = -low;
  bool any = false;
  for (const auto &cube : asset.boneMeshes[source].cubes)
  {
    const auto matrix = intoAnchor * cube.restLocalMatrix;
    const auto &vertices = cube.interleavedPosUv;
    for (size_t i = 0; i + 4 < vertices.size(); i += 5)
    {
      const auto point = glm::vec3(
          matrix * glm::vec4(vertices[i], vertices[i + 1], vertices[i + 2], 1));
      low = glm::min(low, point);
      high = glm::max(high, point);
      any = true;
    }
  }
  return any;
}
} // namespace cutum
