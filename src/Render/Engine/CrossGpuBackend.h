#ifndef CROSSGPUBACKEND_H
#define CROSSGPUBACKEND_H

#include "Render/Mesh/CrossInstanceBatch.h"
#include "World/Math/BlockTypes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

struct CrossGpuBatch
{
  BlockId blockId{BLOCK_AIR};
  size_t instanceCount{0};
  GLuint vao{0};
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
  void RefreshPass(CrossGpuPassCache &cache,
                   const std::vector<CrossInstanceBatch> &batches,
                   uint64_t mesh_revision, uint64_t cull_revision);
  void DestroyPass(CrossGpuPassCache &cache);
  void DestroyAll(CrossGpuPassCache &cache);

  GLuint GetTemplateVao() const { return TemplateVao; }
  GLsizei GetTemplateIndexCount() const { return TemplateIndexCountGl; }

private:
  void UploadInstances(CrossGpuBatch &gpu, const CrossInstanceBatch &batch);
  void EnsureBatchVao(CrossGpuBatch &gpu);
  void DestroyInstanceBuffer(CrossGpuBatch &batch);
  void UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);

  GLuint TemplateVao{0};
  GLuint TemplateVbo{0};
  GLuint TemplateEbo{0};
  GLsizei TemplateIndexCountGl{0};
};

} // namespace cutum

#endif
