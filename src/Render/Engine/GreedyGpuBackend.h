#ifndef GREEDYGPUBACKEND_H
#define GREEDYGPUBACKEND_H

#include "Render/Engine/GreedyVertexPool.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

class UChunkMeshCache;

struct GreedyGpuBatch
{
  BlockId blockId{BLOCK_AIR};
  glm::ivec3 chunkCoord{0};
  /// Matches GreedyBatchRef.batchIndex for sort-order lookup without reupload.
  uint16_t batchIndex{0};
  size_t vertexCount{0};
  size_t indexCount{0};
  GLuint vbo{0};
  GLuint ebo{0};
  GLsizei indexCountGl{0};
  size_t vboCapacityBytes{0};
  size_t eboCapacityBytes{0};
  bool pooled{false};
  size_t vboByteOffset{0};
  size_t eboByteOffset{0};
  /// Frustum sphere for instance-count cull (xyz center, w radius).
  float cullSphere[4]{0, 0, 0, 0};
  /// Exact chunk AABB for compact cull (matches Frustum::IntersectsChunkAABB).
  float cullAabbMin[3]{0, 0, 0};
  float cullAabbMax[3]{0, 0, 0};
  uint32_t drawInstanceCount{1};
};

/// Optional per-RefreshPassRefs counters (transparent Prepare binds these).
struct GreedyGpuRefreshTelem
{
  int UploadFullN{0};
  int CmdReorderN{0};
  /// 0 ok (reorder), 1 !mesh/pass_geom, 2 dirty, 3 !pool, 4 need_rebuild,
  /// 5 key_miss, 6 leftover — set when sort-only was attempted or gated out.
  int OrderOnlyFailReason{0};
};

enum class TransparentOrderOnlyFailReason : int
{
  Ok = 0,
  MeshNotOk = 1,
  Dirty = 2,
  PoolNotOk = 3,
  NeedRebuild = 4,
  KeyMiss = 5,
  Leftover = 6,
};

struct GreedyGpuPassCache
{
  std::vector<GreedyGpuBatch> batches;
  uint64_t meshRevision{0};
  uint64_t cullRevision{0};
  uint64_t sortRevision{0};
  bool usesVertexPool{false};
  GLuint poolVbo{0};
  GLuint poolEbo{0};
  UGreedyVertexPool VertexPool;
  /// Full-pass 1:1 BatchDrawRecord table (instanceCount from GPU compact).
  GLuint IndirectCmdsBuffer{0};
  size_t IndirectCmdCapacity{0};
  GLuint BatchSphereSsbo{0};
  size_t BatchSphereCapacity{0};
  GLuint CullVisSsbo{0};
  size_t CullVisCapacity{0};
  bool IndirectCullReady{false};
  /// True when IndirectCmdsBuffer is authoritative for MultiDraw ranges.
  bool GpuCompactActive{false};
  /// CPU drawInstanceCount synced from CullVisSsbo (lazy, fallback draws).
  bool CompactVisCpuSynced{false};
};

/// Retained GPU buffers for greedy mesh draws (orphan + subData reuse).
class UGreedyGpuBackend
{
public:
  void RefreshPass(GreedyGpuPassCache &cache,
                   const std::vector<GreedyMeshBatch> &batches,
                   uint64_t mesh_revision, uint64_t cull_revision,
                   uint64_t sort_revision);
  void RefreshPassRefs(GreedyGpuPassCache &cache,
                       const UChunkMeshCache &meshCache,
                       const std::vector<GreedyBatchRef> &refs,
                       uint64_t mesh_revision, uint64_t cull_revision,
                       uint64_t sort_revision);
  void DestroyPass(GreedyGpuPassCache &cache);
  void DestroyAll(GreedyGpuPassCache &opaque, GreedyGpuPassCache &cutout,
                  GreedyGpuPassCache &transparent);

  /// Bind/unbind telem sink for the next RefreshPassRefs calls (nullptr clears).
  static void BindRefreshTelem(GreedyGpuRefreshTelem *telem);

private:
  void UploadBatch(GreedyGpuBatch &gpu, const GreedyMeshBatch &batch,
                   UGreedyVertexPool &pool);
  void UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);
  void DestroyBatchBuffers(GreedyGpuBatch &batch);
  void ReleasePooledBatch(GreedyGpuBatch &batch, UGreedyVertexPool &pool);
  void FillBatchCull(GreedyGpuBatch &dst, const GreedyBatchRef &ref);
};

} // namespace cutum

#endif
