#pragma once

#include "Render/Camera/Frustum.h"
#include "Render/Engine/CpuStagingGpuStore.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

/// MDI-capable store: same upload as staging, DrawIndirect submit helper.
class UMdiVertexPoolStore final : public UCpuStagingGpuStore
{
public:
  ~UMdiVertexPoolStore() override;

  const char *BackendName() const override { return "mdi_vertex_pool"; }

  bool SupportsMultiDrawIndirect() const override { return true; }

  uint64_t LastSubmitDrawCmds() const override { return LastDrawCmds; }

  size_t BuildIndirectCommands(const GreedyGpuPassCache &cache,
                               std::vector<DrawElementsIndirectCommand> &out);

  size_t BuildIndirectCommandsRange(
      const GreedyGpuPassCache &cache, size_t begin, size_t end,
      std::vector<DrawElementsIndirectCommand> &out) override;

  bool SubmitIndirectCommands(
      const std::vector<DrawElementsIndirectCommand> &cmds) override;

  bool SubmitIndirectCommandsGpuRange(const GreedyGpuPassCache &cache,
                                      size_t begin, size_t end) override;

  bool TrySubmitMultiDraw(const GreedyGpuPassCache &cache) override;

  void RefreshPassRefs(GreedyGpuPassCache &cache,
                       const UChunkMeshCache &meshCache,
                       const std::vector<GreedyBatchRef> &refs,
                       uint64_t mesh_revision, uint64_t cull_revision,
                       uint64_t sort_revision) override;

  /// CPU frustum → drawInstanceCount (fallback).
  void ApplyFrustumInstanceCull(GreedyGpuPassCache &cache,
                                const Frustum &frustum,
                                const glm::vec3 &camera_pos,
                                float max_cull_distance);

  /// P2: GPU compact writes instanceCount into 1:1 IndirectCmdsBuffer.
  bool ApplyGpuCompactCull(GreedyGpuPassCache &cache, const Frustum &frustum,
                           const glm::vec3 &camera_pos,
                           float max_cull_distance);

  void *MapBucket(MeshGpuBucketHandle handle, size_t bytes) override;
  void UnmapBucket(MeshGpuBucketHandle handle) override;
  void FlipBucketOwnership(MeshGpuBucketHandle handle) override;

  uint64_t GetMappedUploadFrames() const { return MappedUploadFrames; }

private:
  bool EnsureCullProgram();
  void RebuildIndirectCmdTable(GreedyGpuPassCache &cache);

  GLuint IndirectBuffer{0};
  size_t IndirectCapacityBytes{0};
  uint64_t LastDrawCmds{0};
  uint64_t MappedUploadFrames{0};

  GLuint CullProgram{0};
  GLuint CullFrustumUbo{0};
  bool CullInitAttempted{false};

  std::vector<uint8_t> StagingScratch;
  MeshGpuBucketHandle MappedHandle{};
  GLuint MappedVbo{0};
  size_t MappedVboCapacity{0};
  void *MappedPtr{nullptr};
};

} // namespace cutum
