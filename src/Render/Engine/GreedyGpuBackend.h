#ifndef GREEDYGPUBACKEND_H
#define GREEDYGPUBACKEND_H

#include "Render/Engine/GreedyVertexPool.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

struct GreedyGpuBatch
{
  BlockId blockId{BLOCK_AIR};
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
};

/// Retained GPU buffers for greedy mesh draws (orphan + subData reuse).
class UGreedyGpuBackend
{
public:
  void RefreshPass(GreedyGpuPassCache &cache,
                   const std::vector<GreedyMeshBatch> &batches,
                   uint64_t mesh_revision, uint64_t cull_revision,
                   uint64_t sort_revision);
  void DestroyPass(GreedyGpuPassCache &cache);
  void DestroyAll(GreedyGpuPassCache &opaque, GreedyGpuPassCache &cutout,
                  GreedyGpuPassCache &transparent);

private:
  void UploadBatch(GreedyGpuBatch &gpu, const GreedyMeshBatch &batch);
  void UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);
  void DestroyBatchBuffers(GreedyGpuBatch &batch);

  UGreedyVertexPool VertexPool;
};

} // namespace cutum

#endif
