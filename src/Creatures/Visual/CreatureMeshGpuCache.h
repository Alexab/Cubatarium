#ifndef CREATUREMESHGPUCACHE_H
#define CREATUREMESHGPUCACHE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include "Creatures/Visual/LegacyItemGpuScratch.h"
#include <cstddef>
#include <mutex>
#include <unordered_map>

typedef unsigned int GLuint;

namespace cutum
{

struct CreatureMeshGpuStorage
{
  size_t inlineBytes{}, tableBytes{}, skeletal{}, skinned{}, vertexBytes{}, indexBytes{};
  bool supported{};
};

class CreatureMeshGpuCache
{
public:
  static CreatureMeshGpuCache &Instance();

  GLuint GetOrCreateSkeletalMeshVao(const BoneSkeletonCubeMeshCpu &mesh);
  GLuint GetOrCreateGltfSkinnedMeshVao(const GltfPrimitiveCpu &mesh,
                                       size_t &outIndexCount);
  void DestroyAll();
  bool DrawLegacyEquipment(const BoneSkeletonCubeMeshCpu &mesh);
  CreatureMeshGpuStorage InspectOwnedStorage() const;
  void InspectGpuBuffers(TerrainGpuBufferCensus &out) const;

private:
  CreatureMeshGpuCache() = default;

  struct MeshBuffers
  {
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
    size_t vertexBytes{}, indexCount{};
  };

  using MeshTable = std::unordered_map<size_t, MeshBuffers>;
  MeshTable SkeletalCache;
  MeshTable GltfSkinnedCache;
  LegacyItemGpuScratch EquipmentScratch;
  mutable std::mutex CacheMutex;

  static GLuint UploadBuffers(MeshTable &table, size_t hash, const void *vertices,
      size_t vertexBytes, const std::vector<unsigned int> &indices, bool skinned);
  static void DeleteBuffers(const MeshBuffers &buffers) noexcept;
};

} // namespace cutum

#endif
