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
  /// CPU wall around compact GL command submission (not GPU execution time).
  double LastCullSubmitCpuMs() const { return LastCullSubmitCpuMs_; }
  double LastCullTotalMs() const { return LastCullTotalMs_; }
  double LastCullAabbProbeCpuMs() const { return LastCullAabbProbeCpuMs_; }
  double LastCullFallbackCpuMs() const { return LastCullFallbackCpuMs_; }
  double LastCullSetupCpuMs() const { return LastCullSetupCpuMs_; }
  double LastCullQueryPollCpuMs() const { return LastCullQueryPollCpuMs_; }
  double LastCullPostSubmitCpuMs() const { return LastCullPostSubmitCpuMs_; }
  double LastCullStatsPollCpuMs() const { return LastCullStatsPollCpuMs_; }
  double LastCullStatsFencePollCpuMs() const
  {
    return LastCullStatsFencePollCpuMs_;
  }
  double LastCullStatsBufferReadCpuMs() const
  {
    return LastCullStatsBufferReadCpuMs_;
  }
  double LastCullStatsArmCpuMs() const { return LastCullStatsArmCpuMs_; }
  double LastCullBatchStateCpuMs() const { return LastCullBatchStateCpuMs_; }
  double LastCullPostSubmitOtherCpuMs() const
  {
    return LastCullPostSubmitOtherCpuMs_;
  }
  /// Delayed GPU timestamp when queries available; else unavailable (<0).
  double LastCullGpuExecMs() const;
  bool CullGpuTimingAvailable() const { return CullGpuTimingAvailable_; }

  /// Opt in to repeated exact GPU CullStats samples. The fence uses a zero-time
  /// poll, but the later staging-buffer read can still stall the CPU; keep this
  /// disabled on the normal render path and use RequestCullStatsReadbackOnce()
  /// for explicit diagnostics.
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
  double LastCullTotalMs_{0.0};
  double LastCullAabbProbeCpuMs_{0.0};
  double LastCullFallbackCpuMs_{0.0};
  double LastCullSetupCpuMs_{0.0};
  double LastCullQueryPollCpuMs_{0.0};
  double LastCullPostSubmitCpuMs_{0.0};
  double LastCullStatsPollCpuMs_{0.0};
  double LastCullStatsFencePollCpuMs_{0.0};
  double LastCullStatsBufferReadCpuMs_{0.0};
  double LastCullStatsArmCpuMs_{0.0};
  double LastCullBatchStateCpuMs_{0.0};
  double LastCullPostSubmitOtherCpuMs_{0.0};
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
  bool StagedCullStatsValid_{false};
  uint64_t StagedCullStatsVisible_{0};

  /// Q8: delayed CullStats samples — copy SSBO→staging + fence; poll timeout=0.
  struct CullStatsAsyncRing
  {
    static constexpr int kSlots = 4;
    GLuint Staging[kSlots]{};
    void *Fence[kSlots]{};
    bool Pending[kSlots]{};
    uint64_t FrameIds[kSlots]{};
    GreedyGpuPassId PassIds[kSlots]{};
    int WriteIdx{0};
    bool Initialized{false};
  };
  CullStatsAsyncRing CullStatsAsync_{};

  void EnsureCullStatsAsyncRing();
  void DestroyCullStatsAsyncRing();
  void PollCullStatsAsyncRing();
  void ArmCullStatsAsyncSample(GreedyGpuPassId pass_id);

  std::vector<uint8_t> StagingScratch;
  MeshGpuBucketHandle MappedHandle{};
  GLuint MappedVbo{0};
  size_t MappedVboCapacity{0};
  void *MappedPtr{nullptr};
};

/// Period consume of completed CullStats readbacks (async or legacy).
uint64_t ConsumeGpuCullStatsReadbackCount();
/// Q8: blocking SubData / ClientWaitSync count (should stay ~0 on HUD path).
uint64_t ConsumeCullStatsSyncReadN();
/// Arm one upcoming ApplyGpuCompactCull for an async CullStats sample.
void RequestCullStatsReadbackOnce();

} // namespace cutum
