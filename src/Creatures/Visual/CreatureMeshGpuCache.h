#ifndef CREATUREMESHGPUCACHE_H
#define CREATUREMESHGPUCACHE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <cstddef>
#include <mutex>
#include <unordered_map>

typedef unsigned int GLuint;

namespace cutum
{

class CreatureMeshGpuCache
{
public:
  static CreatureMeshGpuCache &Instance();

  GLuint GetOrCreateSkeletalMeshVao(const BoneSkeletonCubeMeshCpu &mesh);
  GLuint GetOrCreateGltfSkinnedMeshVao(const GltfPrimitiveCpu &mesh,
                                       size_t &outIndexCount);
  void DestroyAll();

private:
  CreatureMeshGpuCache() = default;

  struct SkeletalMeshGpuBuffers
  {
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
  };
  struct GltfSkinnedMeshGpuBuffers
  {
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
    size_t indexCount{0};
  };

  std::unordered_map<size_t, SkeletalMeshGpuBuffers> SkeletalCache;
  std::unordered_map<size_t, GltfSkinnedMeshGpuBuffers> GltfSkinnedCache;
  std::mutex CacheMutex;
};

} // namespace cutum

#endif
