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
  std::vector<CrossLightRange> lightRanges;
};

struct CrossGpuPassCache
{
  template<class Inspector> void InspectCatalogRoots(Inspector &out) const
  {for(const auto &batch:batches)out.CrossBatch(batch);}
  std::vector<CrossGpuBatch> batches;
  uint64_t meshRevision{0};
  uint64_t cullRevision{0};
  // Exact ordered keep/drop mask from the source instances. Readiness can
  // change without either geometry or cull revision changing.
  std::vector<uint8_t> instanceSelection;
};

/// Retained GPU instance buffers for cross vegetation (one draw per block type).
class UCrossGpuBackend
{
public:
  bool EnsureTemplateMesh();
  GLsizei GetTemplateIndexCount(BlockDecorShape shape) const;
  /// Publish buffers, field leases and revisions together. Failure retains
  /// the complete predecessor. Scratch storage is reused across refreshes.
  bool RefreshPass(CrossGpuPassCache &cache,
                   const std::vector<CrossInstanceBatch> &batches,
                   uint64_t candidate_mesh_rev, uint64_t candidate_cull_rev,
                   uint64_t expected_mesh_rev, uint64_t expected_cull_rev,
                   const std::vector<uint8_t> &instance_selection = {});
  void DestroyPass(CrossGpuPassCache &cache);
  void DestroyAll(CrossGpuPassCache &cache);
  void InspectGpuBuffers(const CrossGpuPassCache &cache, class TerrainGpuBufferCensus &out) const;
  /// GLES3 has no base-instance draw. Shift all instance attributes together,
  /// submit this contiguous artifact range, and restore the VAO's zero base.
  bool DrawRange(const CrossGpuBatch &batch, size_t first, size_t count);

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
  bool UploadInstances(CrossGpuBatch &gpu, const CrossInstanceBatch &batch);
  bool EnsureBatchVao(CrossGpuBatch &gpu);
  void BindInstanceAttributes(size_t first);
  void DestroyInstanceBuffer(CrossGpuBatch &batch);
  bool UploadBuffer(GLuint &buffer, size_t &capacity_bytes, unsigned int target,
                    const void *data, size_t byte_size);

  std::array<TemplateMesh, 8> Templates{};
  CrossGpuPassCache Staging;
  void *StagingFence{nullptr};
  // Narrow driver-test seam: inject an actual transfer error after allocation.
  // Runtime never assigns this hook.
  friend struct CrossGpuBackendDriverTestAccess;
  void (*BeforeTransfer)(unsigned, void *){nullptr};
  void *TransferContext{nullptr};
};

} // namespace cutum

#endif
