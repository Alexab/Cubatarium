#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H
#include "App/Settings/RenderSettings.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/ChunkDirtySet.h"
#include "Render/Mesh/ChunkMeshRevisionRegistry.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "Render/Mesh/GpuPackedMeshTypes.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/MeshCaptureStore.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Render/Mesh/GreedyMeshVertex.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Streaming/MeshWorkAdmission.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace cutum
{
struct Frustum;
struct MeshBuildResult;
class IUChunkCull;
class IUChunkMesher;

struct MeshRebuildTickStats
{
  int Completed{0};
  int Scheduled{0};
  int SyncRebuilt{0};
};

struct FaceInstance
{
  glm::mat4 model{1.0f};
  BlockId Id{BLOCK_AIR};
  int faceIndex{0};
  glm::vec2 quadSize{1.0f, 1.0f};
};
using BlockInstance = FaceInstance;
class UBlockRegistry;
class UBlockWorld;
class UChunkMeshCache
{
public:
  void MarkAllDirty();
  void MarkAllDirtyFromWorld(const UBlockWorld &world,
                             bool clear_existing_caches = false);
  void MarkDirty(glm::ivec3 chunkCoord);
  void MarkDirtyPriority(glm::ivec3 chunkCoord);
  /// Fluid column cache invalidation — call on fluid voxel changes only, not
  /// on every mesh remesh.
  void InvalidateFluidSurfaceForChunk(glm::ivec3 chunkCoord);
  /// Once per terrain column (gen/light), not on every cy MarkDirty.
  void InvalidateFluidSurfaceForColumn(glm::ivec3 ground_chunk_coord,
                                       bool include_neighbors = true);
  void RemoveChunk(glm::ivec3 chunkCoord);
  /// Removes all Y slices for a terrain column with one greedy-list invalidation.
  void RemoveColumn(glm::ivec3 ground_coord, int max_cy);
  void RebuildDirtyChunks(UBlockWorld &world, UBlockRegistry &registry,
                          int max_drain_per_frame = 8,
                          int max_schedule_per_frame = 8);
  MeshRebuildTickStats RebuildDirtyChunksWithStats(
      UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
      int max_schedule_per_frame, bool force_sync = false,
      int max_sync_rebuild = -1, double max_sync_ms = 6.0,
      bool skip_gpu_consume = false);
  /// DrainCompleted + ProcessPendingGpuMeshes so admission can Finalize on
  /// post-Finish pending (F0 drain-first).
  int ConsumeGpuApplyBacklog(UBlockWorld &world, UBlockRegistry &registry,
                             int max_drain, int gpu_max, double gpu_budget_ms);
  void RebuildAll(UBlockWorld &world, UBlockRegistry &registry);
  void RebuildChunkImmediate(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 chunkCoord);
  /// Reset per-frame Immediate counters (call at start of TickMeshEmerge).
  void ResetImmediateMeshStats();
  double GetLastMeshImmediateMs() const { return LastMeshImmediateMs; }
  int GetLastMeshImmediateCount() const { return LastMeshImmediateCount; }
  bool HasPendingDirty() const;
  bool HasDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                      int radius_chunks) const;
  /// Dirty chunk count inside Chebyshev radius (lit-but-dirty debt metric).
  int CountDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                       int radius_chunks) const;
  bool HasDirtyInColumnBand(glm::ivec2 ground_xz, int min_y, int max_y) const;
  bool HasPendingAsyncMeshWork() const;
  void WaitForAsyncMeshIdle();
  bool WaitForAsyncMeshIdleFor(std::chrono::milliseconds timeout);
  void CancelAsyncMeshWork();
  void CancelAsyncInFlightKeepDirty();
  /// Cancel in-flight async mesh outside focus; coords stay in Dirty.
  void CancelInFlightOutsideHorizontalRadius(glm::ivec3 focus_ground_chunk,
                                             int radius_chunks);
  void DrainAsyncMeshResults(UBlockWorld &world, UBlockRegistry &registry,
                             int max_per_frame);
  double GetLastFlatRebuildMs() const { return LastFlatRebuildMs; }
  double GetLastMeshSyncMs() const { return LastMeshSyncMs; }
  double GetLastMeshSnapshotMs() const { return LastMeshSnapshotMs; }
  double GetLastMeshDirtyTickMs() const { return LastMeshDirtyTickMs; }
  int GetAsyncInFlightCount() const;
  size_t GetMeshCompletedSize() const;
  size_t GetMeshCompletedCapacity() const;
  uint64_t GetMeshCompletedDiscardedOverflow() const;
  void SetMeshCompletedCapacity(size_t cap);
  uint64_t GetMeshDiscardedLateCount() const;
  uint64_t GetMeshApplyStaleCount() const { return MeshApplyStaleCount; }
  /// Older apply discarded while Active tracks a newer revision (not remesh).
  uint64_t GetMeshApplySupersededCount() const
  {
    return MeshApplySupersededCount;
  }
  size_t GetPendingGpuAppliesCount() const { return PendingGpuApplies.size(); }
  size_t GetPendingGpuQueuedCount() const;
  size_t GetPendingGpuKickedCount() const;
  int GetLastGpuKickN() const { return LastGpuKickN; }
  int GetLastGpuFinishN() const { return LastGpuFinishN; }
  int GetLastGpuFinishNotReadyN() const { return LastGpuFinishNotReadyN; }
  int CountPendingGpuAppliesInHorizontalRadius(glm::ivec3 center_ground_chunk,
                                               int radius_chunks) const;
  int DrainPendingGpuMeshes(UBlockWorld &world, UBlockRegistry &registry,
                            int max_count, double budget_ms);
  size_t GetGreedyCacheSize() const { return GreedyCache.size(); }
  bool HasGreedyMesh(glm::ivec3 chunk_coord) const;
  /// True only when the cache entry has GPU quads or non-empty CPU batches.
  /// Empty placeholders must NOT clear missing-mesh / SoftDefer holes
  /// (manual 215919: place-block remesh instantly fills "invisible" chunk).
  bool HasDrawableGreedyMesh(glm::ivec3 chunk_coord) const;
  /// Drawable OR intentional GPU 0-quad commit (occluded). SoftDefer empty
  /// (HasGreedy, !GpuResident) stays false — rim hole SoT (manual 101824).
  bool HasMeshSatisfyingColumnReady(glm::ivec3 chunk_coord) const;
  /// SoftDeferHeld side-set size (outside-focus !Drawable FirstMesh).
  size_t GetSoftDeferHeldCount() const { return SoftDeferHeld.size(); }
  /// Prefetch immutable Capture into store (MarkRelit / commit). Main only.
  void PrefetchMeshCapture(const UBlockWorld &world, glm::ivec3 chunk_coord);
  void InvalidateMeshCapture(glm::ivec3 chunk_coord);
  UMeshCaptureStore &GetCaptureStore() { return CaptureStore; }
  const UMeshCaptureStore &GetCaptureStore() const { return CaptureStore; }
  bool IsGpuExtractInFlight(glm::ivec3 chunk_coord) const;
  bool IsPendingGpuApply(glm::ivec3 chunk_coord) const;
  /// True only while PendingGpuApply for coord is still Phase::Queued (not
  /// Dispatched/Kicked). Used by sticky Immediate escape — never drop kicked.
  bool IsPendingGpuQueued(glm::ivec3 chunk_coord) const;
  /// True while coord is Kicked or Dispatched in the GPU ring.
  bool IsPendingGpuKickedOrDispatched(glm::ivec3 chunk_coord) const;
  /// Move Queued ticket for coord to front so Kick prefers it (no drop).
  bool PreferKickPendingGpuQueued(glm::ivec3 chunk_coord);
  /// Drop Queued-only pending for coord so Immediate can rebuild. Returns true
  /// if a Queued entry was erased. Leaves Dispatched/Kicked untouched.
  bool DropQueuedPendingGpuApply(glm::ivec3 chunk_coord);
  /// True if any non-bottom greedy vertex has sky+block light == 0.
  /// −Y bottoms are ignored (normally unlit).
  bool ChunkHasFullyDarkFace(glm::ivec3 chunk_coord) const;
  static bool BatchesHaveFullyDarkFace(
      const std::vector<GreedyMeshBatch> &batches);
  /// Mesh vertex light=0 but current world light at the face air neighbor
  /// is non-zero — stale bake (empty lightmap / missed MarkRelit remesh).
  bool ChunkHasStaleDarkFaces(glm::ivec3 chunk_coord,
                              const UBlockWorld &world) const;

  struct DarkFaceHit
  {
    glm::ivec3 block{0};
    glm::ivec3 chunk{0};
    BlockId blockId{BLOCK_AIR};
    int faceIndex{0};
    float dist{0.0f};
  };
  /// Nearest non-bottom mesh vertex with sky+block light == 0 (diag).
  /// When `world` is set, also splits count into stale-dark (light-field lit)
  /// vs void-edge (both mesh and field dark) for ARCH_D3 edge gates.
  bool FindNearestDarkFaceNear(const glm::vec3 &camera_pos, float max_dist,
                               int chunk_radius, DarkFaceHit &out,
                               int *out_count_near = nullptr,
                               const UBlockWorld *world = nullptr,
                               int *out_stale_dark = nullptr,
                               int *out_void_edge = nullptr) const;

  /// Chunks whose greedy geometry changed since last GPU pool consume.
  void ConsumeGeometryDirtyChunks(
      std::unordered_set<glm::ivec3, IVec3Hash> &out) const;
  bool HasMissingGreedyMeshInHorizontalRadius(const UBlockWorld &world,
                                              glm::ivec3 center_ground_chunk,
                                              int radius_chunks) const;
  bool FindNearestMissingGreedyMesh(const UBlockWorld &world,
                                    glm::ivec3 center_ground_chunk,
                                    int radius_chunks,
                                    glm::ivec3 &out_coord) const;
  /// Invalidate per-frame hole-query memo (call once before streaming/mesh).
  void BeginHoleQueryFrame();
  const MeshRebuildTickStats &GetLastRebuildTickStats() const
  {
    return LastRebuildTickStats;
  }
  size_t GetDirtyCount() const { return Dirty.GetCount(); }
  void ReserveDirtyCapacity(size_t n) { Dirty.ReserveCapacity(n); }
  int MaybeDropFarthestDirty(glm::ivec3 focus_ground_chunk, size_t soft_cap,
                             int min_keep_horiz = 1);
  bool IsChunkMeshDirty(glm::ivec3 chunk_coord) const;
  uint64_t GetChunkMeshRevision(glm::ivec3 chunk_coord) const;
  bool HasInflightMeshBuild(glm::ivec3 chunk_coord) const;
  /// Drop stale async apply for this chunk (revision bump + clear RemeshAfterApply).
  void InvalidateInFlightMeshBuild(glm::ivec3 chunk_coord);
  uint64_t GetInflightSourceRevision(glm::ivec3 chunk_coord) const;
  size_t GetInstanceCount() const { return Instances.size(); }
  size_t GetGreedyVertexCount() const;
  uint64_t GetMeshRevision() const { return MeshRevision; }
  uint64_t GetCullRevision() const { return CullRevision; }
  void UpdateVisibleInstances(const Frustum &frustum, const glm::mat4 &viewProj,
                              const glm::vec3 &cameraPos);
  /// Public cull entry for IUChunkCull backends (rebuilds flat greedy refs).
  void RebuildGreedyVisibleForCull(const Frustum *frustum,
                                   const glm::vec3 *camera_pos,
                                   float max_cull_distance);

  struct CullSphereEntry
  {
    glm::vec4 sphere{0.0f}; // xyz center, w radius
    glm::ivec3 coord{0};
  };
  /// Fill one sphere per greedy chunk for GPU frustum compaction.
  void CollectGreedyCullSpheres(std::vector<CullSphereEntry> &out) const;
  /// Rebuild flat greedy refs for chunks marked visible (parallel to Collect).
  void RebuildFlatGreedyFromVisibilityMask(const uint32_t *vis,
                                           size_t vis_count,
                                           const std::vector<CullSphereEntry> &entries);

  /// Bound once from RenderBackendFactory (non-owning).
  void SetCullBackend(IUChunkCull *cull) { CullBackend = cull; }
  void SetMesherBackend(IUChunkMesher *mesher) { MesherBackend = mesher; }
  IUChunkCull *GetCullBackend() const { return CullBackend; }
  IUChunkMesher *GetMesherBackend() const { return MesherBackend; }
  double GetLastGpuCullMs() const { return LastGpuCullMs; }
  void SetRenderSettings(const RenderSettings &settings);
  const RenderSettings &GetRenderSettings() const { return Render; }
  void SetRenderDistanceChunks(int distance)
  {
    RenderDistanceChunks = distance;
  }
  void SetMeshRebuildFocus(glm::ivec3 ground_chunk_coord, int radius_chunks);
  /// preferred_cy: sea/surface slice; prefer_lower_cy=true when camera underwater.
  void SetMeshVerticalPriority(int preferred_cy, bool prefer_lower_cy)
  {
    MeshVerticalPreferredCy = preferred_cy;
    MeshPreferLowerCy = prefer_lower_cy;
    MeshVerticalPriorityValid = true;
  }
  void ClearMeshVerticalPriority() { MeshVerticalPriorityValid = false; }
  /// Soft motion/view bias for Dirty sort (Chebyshev units).
  void SetMeshForwardBias(float bias_k, glm::vec2 forward_xz)
  {
    MeshForwardBiasK = bias_k;
    MeshForwardXz = forward_xz;
  }
  /// When true for a chunk coord, SyncRebuildVisibleMissing skips rebuild (await light).
  void SetDeferMeshUntilLitFn(std::function<bool(glm::ivec3)> fn)
  {
    DeferMeshUntilLit = std::move(fn);
  }
  /// When true, skip outside-focus dirty trickle (near holes / pending light).
  void SetStarveOutsideFocusMesh(bool starve) { StarveOutsideFocusMesh = starve; }
  /// When true, skip remesh (already has greedy) until holes clear.
  void SetStarveRemeshForHoles(bool starve) { StarveRemeshForHoles = starve; }
  /// Keep remesh within this Chebyshev radius while StarveRemeshForHoles
  /// (neighbor black-face repair beside holes; manual 090713).
  void SetStarveRemeshKeepHoriz(int keep_h)
  {
    StarveRemeshKeepHoriz = std::max(0, keep_h);
  }
  void SetMeshWorkAdmission(const MeshWorkAdmission &adm)
  {
    WorkAdmission = adm;
    DirtyAdmitRemaining = std::max(0, adm.dirty_admit_budget);
    EnqueueGpuRemaining = std::max(0, adm.enqueue_gpu_budget);
  }
  const MeshWorkAdmission &GetMeshWorkAdmission() const { return WorkAdmission; }
  bool TryConsumeDirtyAdmit()
  {
    if (DirtyAdmitRemaining <= 0)
    {
      return false;
    }
    --DirtyAdmitRemaining;
    return true;
  }
  bool TryConsumeEnqueueGpu()
  {
    if (EnqueueGpuRemaining <= 0)
    {
      return false;
    }
    --EnqueueGpuRemaining;
    return true;
  }
  /// Drop Dirty beyond keep shell (Chebyshev horiz + optional |cy|).
  /// remesh_only: only drop entries that already have greedy mesh (or are in
  /// RemeshAfterApply) — safe on cruise; idle F2 fd_end uses remesh_only=false
  /// to also clear first-mesh/air Dirty outside the eye shell.
  int DropRemeshDirtyBeyondRadius(glm::ivec3 center_chunk, int keep_radius,
                                  int keep_cy = -1, bool remesh_only = false);
  /// Chebyshev radius for SyncRebuildVisibleMissing hole-fill (1=underfeet).
  void SetSyncHoleFillRadius(int radius_chunks)
  {
    SyncHoleFillRadius = std::max(0, radius_chunks);
  }
  /// Cap outside-focus dirty schedules per frame (0 = hard deny).
  void SetMaxOutsideFocusMeshPerFrame(int count)
  {
    MaxOutsideFocusMeshPerFrame = std::max(0, count);
  }
  /// Reserved focus schedules behind movement forward (cruise rear catch-up).
  void SetMaxRearFocusMeshPerFrame(int count)
  {
    MaxRearFocusMeshPerFrame = std::max(0, count);
  }
  /// Queue remesh after in-flight Apply (light changed mid-build).
  void RequestRemeshAfterApply(glm::ivec3 chunk_coord)
  {
    RemeshAfterApply.insert(chunk_coord);
  }
  /// When >= 0, prefer scheduling within this Chebyshev distance. Chunks
  /// farther may still schedule up to MeshScheduleOverflowPerFrame (soft prefer).
  void SetMeshScheduleMaxHorizontalDist(int radius_chunks)
  {
    MeshScheduleMaxHorizontalDist = radius_chunks;
  }
  void SetMeshSnapshotBudgetMs(double ms)
  {
    MeshSnapshotBudgetMs = std::max(1.0, ms);
  }
  void SetMeshEmergeTotalBudgetMs(double ms)
  {
    MeshEmergeTotalBudgetMs = std::max(5.0, ms);
  }
  double GetMeshEmergeTotalBudgetMs() const { return MeshEmergeTotalBudgetMs; }
  void SetMeshScheduleOverflowPerFrame(int count)
  {
    MeshScheduleOverflowPerFrame = std::max(0, count);
  }
  void SetAltitudeCullState(float altitude_above_terrain, int threshold_blocks)
  {
    AltitudeAboveTerrain = altitude_above_terrain;
    AltitudeFogThresholdBlocks = threshold_blocks;
  }
  float GetAltitudeAboveTerrain() const { return AltitudeAboveTerrain; }
  int GetAltitudeFogThresholdBlocks() const
  {
    return AltitudeFogThresholdBlocks;
  }
  void SetSurfaceWetness(float value)
  {
    SurfaceWetness = std::clamp(value, 0.0f, 1.0f);
  }
  int GetRenderDistanceChunks() const { return RenderDistanceChunks; }
  const FluidSurfaceColumnSlice *
  GetFluidSurfaceSlice(const UBlockWorld &world, UBlockRegistry &registry,
                       glm::ivec3 groundChunkCoord, int scanHintY);
  const std::unordered_set<glm::ivec3, IVec3Hash> &
  GetFluidSurfaceDirtyGroundChunks() const
  {
    return FluidSurfaceDirty;
  }
  const std::vector<FaceInstance> &GetFaceInstances() const
  {
    return Instances;
  }
  const std::vector<FaceInstance> &GetInstances() const { return Instances; }
  const std::vector<GreedyBatchRef> &GetGreedyOpaqueCutoutRefs() const
  {
    return GreedyOpaqueCutoutRefs;
  }
  const std::vector<GreedyBatchRef> &GetGreedyTransparentRefs() const
  {
    return GreedyTransparentRefs;
  }
  const std::vector<GpuPackedChunkRef> &GetGpuPackedOpaqueRefs() const
  {
    return GpuPackedOpaqueRefs;
  }
  const std::vector<GpuPackedChunkRef> &GetGpuPackedTransparentRefs() const
  {
    return GpuPackedTransparentRefs;
  }
  UGpuMeshPipeline *GetGpuMeshPipeline();
  const UGpuMeshPipeline *GetGpuMeshPipeline() const;
  const GreedyMeshBatch *TryGetGreedyBatch(const GreedyBatchRef &ref) const
  {
    const auto it = GreedyCache.find(ref.chunkCoord);
    if (it == GreedyCache.end())
    {
      return nullptr;
    }
    const std::vector<GreedyMeshBatch> &batches = it->second.batches;
    if (ref.batchIndex >= batches.size())
    {
      return nullptr;
    }
    return &batches[ref.batchIndex];
  }
  const std::vector<CrossInstanceBatch> &GetCrossBatches() const
  {
    return CrossBatches;
  }
  bool UseHorizontalCullDistance() const
  {
    return AltitudeAboveTerrain >
           static_cast<float>(AltitudeFogThresholdBlocks);
  }
  /// Render-horizon distance admit for frustum cull (same as RebuildVisible).
  float MaxCullDistance() const;

private:
  struct ChunkGreedyMesh
  {
    std::vector<GreedyMeshBatch> batches;
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
    bool GpuResident{false};
    int GpuSlotIndex{-1};
    uint32_t GpuQuadCount{0};
    bool GpuTransparent{false};
    bool GpuHasDarkFace{false};
    std::vector<GpuBlockDrawRange> GpuBlockRanges;
  };
  struct PendingGpuApply
  {
    enum class Phase : uint8_t
    {
      Queued = 0,
      Dispatched = 1, // greedy done, awaiting counter poll + emit
      Kicked = 2,     // quads in PBO, awaiting Finish
    };
    glm::ivec3 coord{0};
    uint64_t sourceRevision{0};
    ChunkMeshSnapshot snapshot;
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
    Phase phase{Phase::Queued};
    bool transparent{false};
    UGpuMeshPipeline::GpuApplyTicket ticket{};
  };
  void EnsureGpuPipeline();
  bool CommitGpuMeshResult(
      const UBlockWorld &world, UBlockRegistry &registry, glm::ivec3 coord,
      uint64_t source_revision, GpuMeshProcessResult &&gpu_result,
      std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> cross_centers);
  int ProcessPendingGpuMeshes(UBlockWorld &world, UBlockRegistry &registry,
                              int max_count, double budget_ms,
                              MeshRebuildTickStats &stats);
  void RebuildChunk(const UBlockWorld &world, UBlockRegistry &registry,
                    glm::ivec3 chunkCoord);
  void ApplyMeshResult(const UBlockWorld &world, UBlockRegistry &registry,
                       MeshBuildResult &&result);
  void EnsureAsyncBuilder();
  void RebuildChunkLegacy(const UBlockWorld &world, UBlockRegistry &registry,
                          glm::ivec3 chunkCoord,
                          std::vector<FaceInstance> &chunkInstances);
  void RebuildFlatInstanceList(const Frustum *frustum,
                               const glm::vec3 *cameraPos,
                               float maxCullDistance);
  void RebuildFlatGreedyBatches(const Frustum *frustum,
                                const glm::vec3 *cameraPos,
                                float maxCullDistance);
  void RebuildFlatCrossInstances(const Frustum *frustum,
                                 const glm::vec3 *cameraPos,
                                 float maxCullDistance);
  void InvalidateVisibleList();
  void TouchPendingGpuIndex() { ++PendingGpuMutationEpoch; }
  void EnsurePendingGpuIndex() const;
  std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> Cache;
  std::unordered_map<glm::ivec3, ChunkGreedyMesh, IVec3Hash> GreedyCache;
  UChunkDirtySet Dirty;
  std::vector<FaceInstance> Instances;
  std::vector<GreedyBatchRef> GreedyOpaqueCutoutRefs;
  std::vector<GreedyBatchRef> GreedyTransparentRefs;
  std::vector<GpuPackedChunkRef> GpuPackedOpaqueRefs;
  std::vector<GpuPackedChunkRef> GpuPackedTransparentRefs;
  std::vector<CrossInstanceBatch> CrossBatches;
  bool InstancesDirty{true};
  bool GreedyBatchesDirty{true};
  bool CrossBatchesDirty{true};
  uint64_t MeshRevision{0};
  uint64_t CullRevision{0};
  glm::ivec3 LastCullCameraChunk{INT32_MAX, INT32_MAX, INT32_MAX};
  uint64_t LastCullMeshRevision{0};
  uint64_t LastVisibleMeshRevision{0};
  std::vector<glm::ivec3> LastVisibleChunks;
  int RenderDistanceChunks{4};
  float AltitudeAboveTerrain{0.0f};
  int AltitudeFogThresholdBlocks{32};
  float SurfaceWetness{0.0f};
  RenderSettings Render;
  std::unique_ptr<UAsyncMeshBuilder> AsyncBuilder;
  std::unique_ptr<UGpuMeshPipeline> GpuPipeline;
  UMeshCaptureStore CaptureStore;
  int CaptureRefreshBudgetLeft{4};
  bool GpuPipelineInitAttempted{false};
  std::deque<PendingGpuApply> PendingGpuApplies;
  mutable std::unordered_map<glm::ivec3, PendingGpuApply::Phase, IVec3Hash>
      PendingGpuIndex;
  uint64_t PendingGpuMutationEpoch{0};
  mutable uint64_t PendingGpuIndexEpoch{0};
  std::unordered_set<glm::ivec3, IVec3Hash> GpuExtractInFlight;
  double LastFlatRebuildMs{0.0};
  double LastGpuCullMs{0.0};
  double LastMeshSyncMs{0.0};
  double LastMeshSnapshotMs{0.0};
  double LastMeshDirtyTickMs{0.0};
  double LastMeshImmediateMs{0.0};
  int LastMeshImmediateCount{0};
  uint64_t MeshApplyStaleCount{0};
  uint64_t MeshApplySupersededCount{0};
  IUChunkCull *CullBackend{nullptr};
  IUChunkMesher *MesherBackend{nullptr};
  std::chrono::steady_clock::time_point LastFlatRebuildAt{};
  bool PendingMeshRevisionBump{false};
  UChunkMeshRevisionRegistry MeshRevisions;
  std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> ActiveMeshSourceRevision;
  std::unordered_map<glm::ivec3, size_t, IVec3Hash> GreedyVertexCountByChunk;
  size_t GreedyVertexCountTotal{0};
  MeshRebuildTickStats LastRebuildTickStats{};
  int LastGpuKickN{0};
  int LastGpuFinishN{0};
  int LastGpuFinishNotReadyN{0};
  std::unordered_map<glm::ivec3, FluidSurfaceColumnSlice, IVec3Hash>
      FluidSurfaceCache;
  std::unordered_set<glm::ivec3, IVec3Hash> FluidSurfaceDirty;
  void BumpMeshRevisionIfNeeded();
  void BumpChunkMeshRevision(glm::ivec3 chunk_coord);
  void RebuildFluidSurfaceSlice(const UBlockWorld &world,
                                UBlockRegistry &registry,
                                glm::ivec3 groundChunkCoord, int scanHintY);
  size_t TotalCrossCenterCount() const;
  bool TrySkipFlatRebuildForVisibleChunks(const Frustum *frustum,
                                          const glm::vec3 *cameraPos,
                                          float maxCullDistance);
  int SyncRebuildVisibleMissing(UBlockWorld &world, UBlockRegistry &registry,
                                int max_sync, double max_ms = 0.0);
  glm::ivec3 MeshFocusGroundChunk{0};
  int MeshFocusRadiusChunks{6};
  bool MeshFocusValid{false};
  int MeshVerticalPreferredCy{0};
  bool MeshPreferLowerCy{false};
  bool MeshVerticalPriorityValid{false};
  float MeshForwardBiasK{0.0f};
  glm::vec2 MeshForwardXz{0.0f};
  bool StarveOutsideFocusMesh{false};
  bool StarveRemeshForHoles{false};
  int StarveRemeshKeepHoriz{2};
  MeshWorkAdmission WorkAdmission{};
  int DirtyAdmitRemaining{8};
  int EnqueueGpuRemaining{8};
  /// SyncRebuildVisibleMissing: fill missing within this Chebyshev radius.
  int SyncHoleFillRadius{1};
  int MaxOutsideFocusMeshPerFrame{2};
  int MaxRearFocusMeshPerFrame{0};
  std::unordered_set<glm::ivec3, IVec3Hash> RemeshAfterApply;
  /// SoftDefer dropped !Drawable FirstMesh outside focus — requeue when
  /// MayMesh / focus admits (rim plan B4; avoid forever-RemoveAt).
  std::unordered_set<glm::ivec3, IVec3Hash> SoftDeferHeld;
  void HoldSoftDeferFirstMesh(glm::ivec3 chunk_coord);
  void RequeueSoftDeferHeld();
  /// GPU pool incremental upload: chunks mutated since last Consume.
  mutable std::unordered_set<glm::ivec3, IVec3Hash> GeometryDirtyChunks;
  void NoteGeometryDirty(glm::ivec3 chunk_coord);
  /// -1 = no extra horizontal schedule cap (only focus starve applies).
  int MeshScheduleMaxHorizontalDist{-1};
  double MeshSnapshotBudgetMs{6.0};
  double MeshEmergeTotalBudgetMs{25.0};
  /// When MaxHorizontalDist >= 0, allow this many farther schedules/frame.
  int MeshScheduleOverflowPerFrame{0};
  std::function<bool(glm::ivec3)> DeferMeshUntilLit;
  // Per-DoMovement memo: HasMissing/FindNearest are called many times/frame.
  mutable uint64_t HoleQueryEpoch{0};
  struct MissingQueryMemo
  {
    uint64_t epoch{0};
    glm::ivec3 center{0};
    int radius{-1};
    bool result{false};
  };
  mutable MissingQueryMemo MissingMemo{};
  struct NearestQueryMemo
  {
    uint64_t epoch{0};
    glm::ivec3 center{0};
    int radius{-1};
    bool found{false};
    glm::ivec3 coord{0};
  };
  mutable NearestQueryMemo NearestMemo{};
};
} // namespace cutum
#endif
