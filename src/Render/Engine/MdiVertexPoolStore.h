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
                                float max_cull_distance,
                                bool horizontal_distance = false);

  /// P2: GPU compact writes instanceCount into 1:1 IndirectCmdsBuffer.
  bool ApplyGpuCompactCull(GreedyGpuPassCache &cache, const Frustum &frustum,
                           const glm::vec3 &camera_pos,
                           float max_cull_distance,
                           bool horizontal_distance = false);

  /// Lazy readback of compact vis for DrawElementsBaseVertex fallback.
  bool SyncCompactVisToCpu(GreedyGpuPassCache &cache);

  void *MapBucket(MeshGpuBucketHandle handle, size_t bytes) override;
  void UnmapBucket(MeshGpuBucketHandle handle) override;
  void FlipBucketOwnership(MeshGpuBucketHandle handle) override;

  uint64_t GetMappedUploadFrames() const { return MappedUploadFrames; }
  uint64_t LastCullOpaqueTotal() const { return LastCullOpaqueTotal_; }
  uint64_t LastCullOpaqueOn() const { return LastCullOpaqueOn_; }
  uint64_t LastCpuAabbWouldOn() const { return LastCpuAabbWouldOn_; }
  /// CPU wall around compact dispatch (no SubData); 0 on CPU cull path.
  double LastCompactCullGpuMs() const { return LastCompactCullGpuMs_; }

  /// Enable rare CullStatsSsbo GetBufferSubData (default off — hot path free).
  void SetCullStatsReadbackEnabled(bool enabled)
  {
    CullStatsReadbackEnabled_ = enabled;
  }

private:
  bool EnsureCullProgram();
  void RebuildIndirectCmdTable(GreedyGpuPassCache &cache);

  GLuint IndirectBuffer{0};
  size_t IndirectCapacityBytes{0};
  uint64_t LastDrawCmds{0};
  uint64_t MappedUploadFrames{0};
  uint64_t LastCullOpaqueTotal_{0};
  uint64_t LastCullOpaqueOn_{0};
  uint64_t LastCpuAabbWouldOn_{0};
  double LastCompactCullGpuMs_{0.0};

  GLuint CullProgram{0};
  GLuint CullFrustumUbo{0};
  GLuint CullAabbMaxSsbo{0};
  size_t CullAabbMaxCapacity{0};
  GLuint CullStatsSsbo{0};
  bool CullInitAttempted{false};
  bool CullProgramIsSphere{false};
  bool CullStatsReadbackEnabled_{false};

  std::vector<uint8_t> StagingScratch;
  MeshGpuBucketHandle MappedHandle{};
  GLuint MappedVbo{0};
  size_t MappedVboCapacity{0};
  void *MappedPtr{nullptr};
};

/// Period consume of CullStatsSsbo GetBufferSubData count (sync readback).
uint64_t ConsumeGpuCullStatsReadbackCount();
/// Arm one upcoming ApplyGpuCompactCull to SubData CullStats (period/HUD).
void RequestCullStatsReadbackOnce();

} // namespace cutum
