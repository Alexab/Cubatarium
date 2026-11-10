#ifndef CREATUREMESHGPUHASH_H
#define CREATUREMESHGPUHASH_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <cstddef>
#include <functional>

namespace cutum
{

/// Content hash keying the GPU VAO caches. Computed once when an asset is
/// built or loaded and stored in the mesh (GpuHash) — never per draw call
/// (audit V4: re-hashing every vertex float every frame).
inline size_t HashCreatureMeshContent(const std::vector<float> &posUv,
                                      const std::vector<unsigned int> &indices)
{
  size_t hash = 0;
  for (float v : posUv)
  {
    hash = hash * 31 + std::hash<float>{}(v);
  }
  for (unsigned int idx : indices)
  {
    hash = hash * 31 + idx;
  }
  return hash;
}

inline size_t SkeletalMeshGpuHash(const BoneSkeletonCubeMeshCpu &mesh)
{
  return HashCreatureMeshContent(mesh.interleavedPosUv, mesh.indices);
}

inline size_t GltfSkinnedMeshGpuHash(const GltfPrimitiveCpu &mesh)
{
  size_t hash = HashCreatureMeshContent(mesh.mesh.interleavedPosUv,
                                        mesh.mesh.indices);
  for (uint8_t j : mesh.jointIndices)
  {
    hash = hash * 31 + j;
  }
  for (float w : mesh.jointWeights)
  {
    hash = hash * 31 + std::hash<float>{}(w);
  }
  return hash;
}

} // namespace cutum

#endif
