#ifndef CROSSGPUBACKEND_H
#define CROSSGPUBACKEND_H

#include "Render/Mesh/CrossInstanceBatch.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

struct CrossGpuBatch
{
  BlockId blockId{BLOCK_AIR};
  BlockDecorShape shape{BlockDecorShape::Cross};
  size_t instanceCount{0};
  GLuint vao{0};
  BlockDecorShape vaoShape{BlockDecorShape::Cross};
  GLuint instanceVbo{0};
  size_t instanceCapacityBytes{0};
};

struct CrossGpuPassCache
{
  std::vector<CrossGpuBatch> batches;
  uint64_t meshRevision{0};
  uint64_t cullRevision{0};
};

/// Retained GPU instance buffers for cross vegetation (one draw per block type).
class UCrossGpuBackend
{
public:
  bool EnsureTemplateMesh();
  GLsizei GetTemplateIndexCount(BlockDecorShape shape) const;
  void RefreshPass(CrossGpuPassCache &cache,
                   const std::vector<CrossInstanceBatch> &batches,
                   uint64_t candidate_mesh_rev, uint64_t candidate_cull_rev,
                   uint64_t expected_mesh_rev, uint64_t expected_cull_rev);
  void DestroyPass(CrossGpuPassCache &cache);
  void DestroyAll(CrossGpuPassCache &cache);

  GLuint GetTemplateVao() const { return Templates[0].vao; }
  GLsizei GetTemplateIndexCount() const
  {
    return Templates[0].indexCount;
  }

private:
  struct TemplateMesh
  {
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
    GLsizei indexCount{0};
  };
  bool EnsureTemplateMesh(BlockDecorShape shape);
  void UploadInstances(CrossGpuBatch &gpu, const CrossInstanceBatch &batch);
  void EnsureBatchVao(CrossGpuBatch &gpu);
  void DestroyInstanceBuffer(CrossGpuBatch &batch);
  void UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);

  std::array<TemplateMesh, 6> Templates{};
};

} // namespace cutum

#endif
