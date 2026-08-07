#ifndef WORLDMESHSERVICE_H
#define WORLDMESHSERVICE_H

#include "App/Settings/RenderSettings.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "World/Interfaces/IUWorldMeshSink.h"
#include "World/Mesh/DigSeamQueue.h"
#include "World/Streaming/MeshWorkAdmission.h"
#include <chrono>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UCamera;
class IUChunkCull;
class IUChunkMesher;
struct Frustum;
struct PhysicsTelemetry;

/// Owns chunk mesh cache; bridge between World block data and Render meshing.
class UWorldMeshService
{
public:
  UWorldMeshService();

  UChunkMeshCache &GetCache() { return Cache; }
  const UChunkMeshCache &GetCache() const { return Cache; }

  void SetMeshSink(IUWorldMeshSink *sink) { MeshSink = sink; }

  /// When true, edit remesh prefers center Immediate + Dirty ring (GPU store).
  void SetPreferGpuStorePatch(bool enabled) { PreferGpuStorePatch = enabled; }
  bool GetPreferGpuStorePatch() const { return PreferGpuStorePatch; }
  uint64_t GetLastEditImmediateN() const { return LastEditImmediateN; }
  uint64_t GetLastEditDirtyN() const { return LastEditDirtyN; }

  void SetCullBackend(IUChunkCull *cull) { Cache.SetCullBackend(cull); }
  void SetMesherBackend(IUChunkMesher *mesher)
  {
    Cache.SetMesherBackend(mesher);
  }
  double GetLastGpuCullMs() const { return Cache.GetLastGpuCullMs(); }

  void SetRenderSettings(const RenderSettings &settings);
  void SetRenderDistanceChunks(int distance);
  void SetMeshRebuildFocus(glm::ivec3 ground_chunk_coord, int radius_chunks);
  void SetMeshVerticalPriority(int preferred_cy, bool prefer_lower_cy);
  void ClearMeshVerticalPriority();
  void SetMeshForwardBias(float bias_k, glm::vec2 forward_xz);
  void SetDeferMeshUntilLitFn(std::function<bool(glm::ivec3)> fn);
  void SetStarveOutsideFocusMesh(bool starve);
  void SetStarveRemeshForHoles(bool starve);
  void SetStarveRemeshKeepHoriz(int keep_h);
  void SetMeshWorkAdmission(const MeshWorkAdmission &adm);
  const MeshWorkAdmission &GetMeshWorkAdmission() const;
  /// Consume one Dirty-admit slot for FirstMesh/Held/neighbor (false = deny).
  bool TryConsumeDirtyAdmit();
  int DropRemeshDirtyBeyondRadius(glm::ivec3 center_chunk, int keep_radius,
                                  int keep_cy = -1, bool remesh_only = false);
  void SetSyncHoleFillRadius(int radius_chunks);
  void SetMaxOutsideFocusMeshPerFrame(int count);
  void SetMaxRearFocusMeshPerFrame(int count);
  void SetMeshScheduleMaxHorizontalDist(int radius_chunks);
  void SetMeshScheduleOverflowPerFrame(int count);
  void SetMeshSnapshotBudgetMs(double ms);
  void SetMeshEmergeTotalBudgetMs(double ms);
  double GetMeshEmergeTotalBudgetMs() const;
  void SetAltitudeCullState(float altitude_above_terrain, int threshold_blocks);

  void MarkDirty(glm::ivec3 chunk_coord);
  void MarkDirtyPriority(glm::ivec3 chunk_coord);
  void PrefetchMeshCapture(const UBlockWorld &world, glm::ivec3 chunk_coord);
  void PrefetchMeshCaptureBand(const UBlockWorld &world,
                               glm::ivec3 ground_chunk_coord, int min_y,
                               int max_y);
  void RequestRemeshAfterApply(glm::ivec3 chunk_coord);
  /// Invalidate fluid surface column cache when this block or a neighbor is liquid.
  void NotifyFluidSurfaceDirtyAtBlock(const UBlockWorld &world,
                                      UBlockRegistry *registry,
                                      glm::ivec3 block_pos);
  void InvalidateFluidSurfaceForColumn(glm::ivec3 ground_chunk_coord,
                                       bool include_neighbors = true);
  void MarkAllDirtyFromWorld(const UBlockWorld &world);
  void RemoveChunk(glm::ivec3 chunk_coord);
  void RemoveColumn(glm::ivec3 ground_coord, int max_cy);
  void MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y);
  void MarkTerrainChunkMeshDirty(glm::ivec3 ground_chunk_coord, int min_y,
                                 int max_y);
  void MarkTerrainChunkMeshDirtySeamed(glm::ivec3 ground_chunk_coord, int min_y,
                                       int max_y,
                                       bool include_horizontal_neighbors = true);
  void MarkTerrainChunkMeshDirtyPriority(glm::ivec3 ground_chunk_coord, int min_y,
                                         int max_y);
  void MarkTerrainChunkMeshDirtySeamedPriority(
      glm::ivec3 ground_chunk_coord, int min_y, int max_y,
      bool include_horizontal_neighbors = true);
  /// Dirty only solid slices in [min_y,max_y] that fail column-ready / not in-flight.
  int MarkMissingSlicesDirtyPriority(const UBlockWorld &world,
                                     glm::ivec3 ground_chunk_coord, int min_y,
                                     int max_y);
  /// Enqueue missing solid slices below placed block for post-place DigSeam.
  int EnqueueColumnMissingDigSeamBelow(const UBlockWorld &world,
                                       glm::ivec3 block_pos,
                                       int max_enqueue = 4);

  void RebuildAll(UBlockWorld &world, UBlockRegistry &registry);
  void RebuildDirtyChunks(UBlockWorld &world, UBlockRegistry &registry,
                          int max_drain_per_frame, int max_schedule_per_frame);
  MeshRebuildTickStats RebuildDirtyChunksWithStats(
      UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
      int max_schedule_per_frame, bool force_sync = false,
      int max_sync_rebuild = -1, double max_sync_ms = 6.0,
      bool skip_gpu_consume = false);
  int ConsumeGpuApplyBacklog(UBlockWorld &world, UBlockRegistry &registry,
                             int max_drain, int gpu_max, double gpu_budget_ms);
  void DrainAsyncMeshResults(UBlockWorld &world, UBlockRegistry &registry,
                             int max_per_frame);
  void RebuildChunkImmediate(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 chunk_coord);
  /// Invalidate inflight/async apply for edit neighborhood (face neighbors).
  void InvalidateEditMeshNeighborhood(
      const std::vector<glm::ivec3> &block_positions);
  void ResetImmediateMeshStats();
  void BeginHoleQueryFrame();
  double GetLastMeshImmediateMs() const;
  int GetLastMeshImmediateCount() const;
  void WaitForAsyncMeshIdle();
  bool WaitForAsyncMeshIdleFor(std::chrono::milliseconds timeout);
  void CancelAsyncMeshWork();
  void CancelAsyncInFlightKeepDirty();
  void CancelInFlightOutsideHorizontalRadius(glm::ivec3 focus_ground_chunk,
                                             int radius_chunks);

  bool HasPendingDirty() const;
  bool HasDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                      int radius_chunks) const;
  int CountDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                       int radius_chunks) const;
  bool HasDirtyInColumnBand(glm::ivec2 ground_xz, int min_y, int max_y) const;
  bool HasPendingAsyncMeshWork() const;
  size_t GetDirtyCount() const;
  void ReserveDirtyCapacity(size_t n);
  int MaybeDropFarthestDirty(glm::ivec3 focus_ground_chunk, size_t soft_cap,
                             int min_keep_horiz = 1);
  int GetAsyncInFlightCount() const;
  size_t GetMeshCompletedSize() const;
  size_t GetMeshCompletedCapacity() const;
  uint64_t GetMeshCompletedDiscardedOverflow() const;
  void SetMeshCompletedCapacity(size_t cap);
  uint64_t GetMeshDiscardedLateCount() const;
  uint64_t GetMeshApplyStaleCount() const;
  size_t GetPendingGpuAppliesCount() const;
  size_t GetPendingGpuQueuedCount() const;
  size_t GetPendingGpuKickedCount() const;
  int GetLastGpuKickN() const;
  int GetLastGpuFinishN() const;
  int GetLastGpuFinishNotReadyN() const;
  int CountPendingGpuAppliesInHorizontalRadius(glm::ivec3 center_ground_chunk,
                                               int radius_chunks) const;
  int DrainPendingGpuMeshes(UBlockWorld &world, UBlockRegistry &registry,
                            int max_count, double budget_ms);
  double GetLastFlatRebuildMs() const;
  double GetLastMeshSyncMs() const;
  double GetLastMeshSnapshotMs() const;
  double GetLastMeshDirtyTickMs() const;
  size_t GetGreedyCacheSize() const;
  bool HasGreedyMesh(glm::ivec3 chunk_coord) const;
  /// True only when cache has GPU quads or non-empty CPU batches (not empty
  /// placeholder entries that SoftDefer treated as "has mesh").
  bool HasDrawableGreedyMesh(glm::ivec3 chunk_coord) const;
  bool HasMeshSatisfyingColumnReady(glm::ivec3 chunk_coord) const;
  size_t GetSoftDeferHeldCount() const;
  bool IsGpuExtractInFlight(glm::ivec3 chunk_coord) const;
  /// Queued in PendingGpuApplies — orphaned GpuExtractInFlight alone is not.
  bool IsPendingGpuApply(glm::ivec3 chunk_coord) const;
  bool IsPendingGpuQueued(glm::ivec3 chunk_coord) const;
  bool IsPendingGpuKickedOrDispatched(glm::ivec3 chunk_coord) const;
  bool PreferKickPendingGpuQueued(glm::ivec3 chunk_coord);
  bool DropQueuedPendingGpuApply(glm::ivec3 chunk_coord);
  bool ChunkHasStaleDarkFaces(glm::ivec3 chunk_coord,
                             const UBlockWorld &world) const;
  bool IsChunkMeshDirty(glm::ivec3 chunk_coord) const;
  uint64_t GetChunkMeshRevision(glm::ivec3 chunk_coord) const;
  bool HasInflightMeshBuild(glm::ivec3 chunk_coord) const;
  uint64_t GetInflightSourceRevision(glm::ivec3 chunk_coord) const;
  bool HasMissingGreedyMeshInHorizontalRadius(const UBlockWorld &world,
                                              glm::ivec3 center_ground_chunk,
                                              int radius_chunks) const;
  bool FindNearestMissingGreedyMesh(const UBlockWorld &world,
                                    glm::ivec3 center_ground_chunk,
                                    int radius_chunks,
                                    glm::ivec3 &out_coord) const;
  /// Track nearest sticky miss for moving Immediate escape (F3).
  void UpdateStickyNearestHole(glm::ivec3 coord, bool alive);
  int GetStickyNearestHoleFrames() const { return StickyNearestHoleFrames; }
  glm::ivec3 GetStickyNearestHoleCoord() const { return StickyNearestHoleCoord; }
  const MeshRebuildTickStats &GetLastRebuildTickStats() const;
  uint64_t GetMeshRevision() const;
  uint64_t GetCullRevision() const;
  size_t GetGreedyVertexCount() const;
  size_t GetInstanceCount() const;

  void UpdateVisibleInstances(const Frustum &frustum,
                              const glm::mat4 &view_proj,
                              const glm::vec3 &camera_pos);
  void WarmupVisibleListFromViewProj(const glm::mat4 &view_proj,
                                     const glm::vec3 &camera_pos);

  const std::vector<FaceInstance> &
  PrepareFaceInstances(UBlockWorld &world, UBlockRegistry &registry,
                       const std::shared_ptr<UCamera> &camera,
                       int max_drain_per_frame = 8,
                       int max_schedule_per_frame = 8);

  const std::vector<GreedyMeshBatch> &
  GetGreedyRenderBatches(UBlockWorld &world, UBlockRegistry &registry,
                         const std::shared_ptr<UCamera> &camera);

  struct GreedyDrawSnapshot
  {
    const UChunkMeshCache &cache;
    const std::vector<GreedyBatchRef> &opaqueCutoutRefs;
    const std::vector<GreedyBatchRef> &transparentRefs;
    const std::vector<CrossInstanceBatch> &crossBatches;
    uint64_t meshRevision{0};
    uint64_t cullRevision{0};
  };
  GreedyDrawSnapshot PrepareGreedyDraw(UBlockWorld &world,
                                       UBlockRegistry &registry,
                                       const std::shared_ptr<UCamera> &camera);

  void MarkBlockChunkDirtyFromEdit(
      UBlockWorld &block_world, UBlockRegistry *registry, glm::ivec3 block_pos,
      std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
      bool sync_neighbor_chunks = false, bool sync_light_ring = false);
  void MarkBlocksChunkDirtyBatchFromEdit(
      UBlockWorld &block_world, UBlockRegistry *registry,
      const std::vector<glm::ivec3> &block_positions,
      std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
      bool sync_neighbor_chunks = false, bool sync_light_ring = false,
      bool collect_break_diag = false, PhysicsTelemetry *break_tele = nullptr);
  void MarkChunksContainingBlockIds(const UBlockWorld &block_world,
                                    const std::vector<BlockId> &block_ids);

  /// DigSeam: P2-demoted face Immediate → guaranteed remesh (manual 215711 X-ray).
  void EnqueueDigSeam(glm::ivec3 chunk_coord);
  size_t GetDigSeamPendingCount() const { return DigSeam.Size(); }
  int GetLastDigSeamRemeshN() const { return LastDigSeamRemeshN; }
  int GetLastDigSeamPendingN() const { return LastDigSeamPendingN; }
  void TickDigSeamDrain(UBlockWorld &block_world, UBlockRegistry &registry,
                        const PhysicsTelemetry *frame_tele);

  const std::vector<CrossInstanceBatch> &
  GetCrossRenderBatches(UBlockWorld &world, UBlockRegistry &registry,
                        const std::shared_ptr<UCamera> &camera);

private:
  void NotifyChunkBlocksChanged(glm::ivec3 chunk_coord);
  void NotifyChunkUnloaded(glm::ivec3 chunk_coord);

  UChunkMeshCache Cache;
  IUWorldMeshSink *MeshSink{nullptr};
  bool PreferGpuStorePatch{false};
  uint64_t LastEditImmediateN{0};
  uint64_t LastEditDirtyN{0};
  glm::ivec3 StickyNearestHoleCoord{0};
  int StickyNearestHoleFrames{0};

  DigSeamQueue DigSeam;
  int LastDigSeamRemeshN{0};
  int LastDigSeamPendingN{0};
};

} // namespace cutum

#endif
