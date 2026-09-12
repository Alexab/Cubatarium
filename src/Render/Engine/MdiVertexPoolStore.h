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
                       uint64_t sort_revision,
                       bool consume_dirty = true) override;

  /// CPU frustum → drawInstanceCount (fallback).
  void ApplyFrustumInstanceCull(GreedyGpuPassCache &cache,
                                const Frustum &frustum,
                                const glm::vec3 &camera_pos,
                                float max_cull_distance,
                                bool horizontal_distance = false);

  /// P2: GPU compact writes instanceCount into 1:1 IndirectCmdsBuffer.
  /// probe_period / force_probe: Phase 5.7R7 fail-open AABB diet (period 6
  /// on healthy cruise; force under underfeet / VB edge).
  bool ApplyGpuCompactCull(GreedyGpuPassCache &cache, const Frustum &frustum,
                           const glm::vec3 &camera_pos,
                           float max_cull_distance,
                           bool horizontal_distance = false,
                           int probe_period = 6, bool force_probe = false);

  /// Lazy readback of compact vis for DrawElementsBaseVertex fallback.
  bool SyncCompactVisToCpu(GreedyGpuPassCache &cache);

  void *MapBucket(MeshGpuBucketHandle handle, size_t bytes) override;
  void UnmapBucket(MeshGpuBucketHandle handle) override;
  void FlipBucketOwnership(MeshGpuBucketHandle handle) override;

  uint64_t GetMappedUploadFrames() const { return MappedUploadFrames; }
  uint64_t LastCullOpaqueTotal() const { return LastCullOpaqueTotal_; }
  uint64_t LastCullOpaqueOn() const { return LastCullOpaqueOn_; }
  uint64_t LastCpuAabbWouldOn() const { return LastCpuAabbWouldOn_; }
  /// CPU wall around compact dispatch + barrier (not GPU execution time).
  double LastCullSubmitCpuMs() const { return LastCullSubmitCpuMs_; }
  /// Delayed GPU timestamp when queries available; else unavailable (<0).
  double LastCullGpuExecMs() const;
  bool CullGpuTimingAvailable() const { return CullGpuTimingAvailable_; }

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
  double LastCullSubmitCpuMs_{0.0};
  bool CullGpuTimingAvailable_{false};

  struct GpuTimestampQueryRing
  {
    static constexpr int kSlots = 8;
    GLuint Queries[kSlots]{};
    int WriteIdx{0};
    uint64_t FrameIds[kSlots]{};
    bool Pending[kSlots]{};
    GreedyGpuPassId PassIds[kSlots]{};
    uint64_t NextSubmission{1};
    bool Initialized{false};
  };
  GpuTimestampQueryRing CullGpuTimeRing_{};
  double LastCullGpuExecMs_{-1.0};
  double ReadyCullGpuMs_[4]{-1.0, -1.0, -1.0, -1.0};
  uint64_t ReadyCullGpuSequence_[4]{};

  void InitCullGpuTimingIfNeeded();
  void BeginCullGpuTimestamp();
  void EndCullGpuTimestamp(uint64_t frame_id);
  void PollCullGpuTimestampRing();

  GLuint CullProgram{0};
  GLuint CullFrustumUbo{0};
  GLuint CullStatsSsbo{0};
  bool CullInitAttempted{false};
  bool CullProgramIsSphere{false};
  bool CullStatsReadbackEnabled_{false};
  bool CullStatsPendingRead_{false};
  bool StagedCullStatsValid_{false};
  uint64_t StagedCullStatsVisible_{0};

  std::vector<uint8_t> StagingScratch;
  MeshGpuBucketHandle MappedHandle{};
  GLuint MappedVbo{0};
  size_t MappedVboCapacity{0};
  void *MappedPtr{nullptr};
};

/// Period consume of CullStatsSsbo GetBufferSubData count (sync readback).
uint64_t ConsumeGpuCullStatsReadbackCount();
/// Q8: sync SubData reads gated to stats-on + delayed sample path.
uint64_t ConsumeCullStatsSyncReadN();
/// Arm one upcoming ApplyGpuCompactCull to SubData CullStats (period/HUD).
void RequestCullStatsReadbackOnce();

} // namespace cutum
