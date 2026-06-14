#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H
#include "World/Math/BlockTypes.h"
#include "World/Chunks/ChunkManager.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "App/Settings/RenderSettings.h"
#include <climits>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace cutum
{
struct Frustum;
struct FaceInstance
{
  glm::mat4 model{1.0f};
  BlockId id{BLOCK_AIR};
  int faceIndex{0};
  glm::vec2 quadSize{1.0f, 1.0f};
};
using BlockInstance = FaceInstance;
struct GreedyMeshBatch
{
  BlockId blockId{BLOCK_AIR};
  bool transparent{false};
  std::vector<GreedyMeshVertex> vertices;
  std::vector<uint32_t> indices;
};
class UBlockRegistry;
class UBlockWorld;
class UChunkMeshCache
{
public:
  void MarkAllDirty();
  void MarkAllDirtyFromWorld(const UBlockWorld &world);
  void MarkDirty(glm::ivec3 chunkCoord);
  void RemoveChunk(glm::ivec3 chunkCoord);
  void RebuildDirtyChunks(UBlockWorld &world, UBlockRegistry &registry,
                          int maxChunksPerFrame = 8);
  void RebuildAll(UBlockWorld &world, UBlockRegistry &registry);
  void RebuildChunkImmediate(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 chunkCoord);
  bool HasPendingDirty() const { return !dirtyChunks_.empty(); }
  size_t GetInstanceCount() const { return instances_.size(); }
  size_t GetGreedyVertexCount() const;
  uint64_t GetMeshRevision() const { return meshRevision_; }
  uint64_t GetCullRevision() const { return cullRevision_; }
  void UpdateVisibleInstances(const Frustum &frustum, const glm::mat4 &viewProj,
                              const glm::vec3 &cameraPos);
  void SetRenderSettings(const RenderSettings &settings);
  void SetRenderDistanceChunks(int distance)
  {
    RenderDistanceChunks = distance;
  }
  const std::vector<FaceInstance> &GetFaceInstances() const
  {
    return instances_;
  }
  const std::vector<FaceInstance> &GetInstances() const { return instances_; }
  const std::vector<GreedyMeshBatch> &GetGreedyBatches() const
  {
    return greedyBatches_;
  }

private:
  struct ChunkGreedyMesh
  {
    std::vector<GreedyMeshBatch> batches;
  };
  void RebuildChunk(const UBlockWorld &world, UBlockRegistry &registry,
                    glm::ivec3 chunkCoord);
  void RebuildChunkLegacy(const UBlockWorld &world, UBlockRegistry &registry,
                          glm::ivec3 chunkCoord,
                          std::vector<FaceInstance> &chunkInstances);
  void RebuildFlatInstanceList(const Frustum *frustum,
                               const glm::vec3 *cameraPos,
                               float maxCullDistance);
  void RebuildFlatGreedyBatches(const Frustum *frustum,
                                const glm::vec3 *cameraPos,
                                float maxCullDistance);
  void InvalidateVisibleList();
  float MaxCullDistance() const;
  std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> cache_;
  std::unordered_map<glm::ivec3, ChunkGreedyMesh, IVec3Hash> greedyCache_;
  std::vector<glm::ivec3> dirtyChunks_;
  std::unordered_set<glm::ivec3, IVec3Hash> dirtyChunkSet_;
  std::vector<FaceInstance> instances_;
  std::vector<GreedyMeshBatch> greedyBatches_;
  bool instancesDirty_{true};
  bool greedyBatchesDirty_{true};
  uint64_t meshRevision_{0};
  uint64_t cullRevision_{0};
  glm::ivec3 lastCullCameraChunk_{INT32_MAX, INT32_MAX, INT32_MAX};
  uint64_t lastCullMeshRevision_{0};
  int RenderDistanceChunks{4};
  RenderSettings Render;
};
} // namespace cutum
#endif
