#ifndef WORLDMESHSERVICE_H
#define WORLDMESHSERVICE_H

#include "App/Settings/RenderSettings.h"
#include "Render/Mesh/ChunkMeshCache.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "World/Interfaces/IUWorldMeshSink.h"
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UCamera;
struct Frustum;

/// Owns chunk mesh cache; bridge between World block data and Render meshing.
class UWorldMeshService
{
public:
  UWorldMeshService();

  UChunkMeshCache &GetCache() { return Cache; }
  const UChunkMeshCache &GetCache() const { return Cache; }

  void SetMeshSink(IUWorldMeshSink *sink) { MeshSink = sink; }

  void SetRenderSettings(const RenderSettings &settings);
  void SetRenderDistanceChunks(int distance);
  void SetMeshRebuildFocus(glm::ivec3 ground_chunk_coord, int radius_chunks);
  void SetAltitudeCullState(float altitude_above_terrain, int threshold_blocks);

  void MarkDirty(glm::ivec3 chunk_coord);
  void MarkDirtyPriority(glm::ivec3 chunk_coord);
  void MarkAllDirtyFromWorld(const UBlockWorld &world);
  void RemoveChunk(glm::ivec3 chunk_coord);
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

  void RebuildAll(UBlockWorld &world, UBlockRegistry &registry);
  void RebuildDirtyChunks(UBlockWorld &world, UBlockRegistry &registry,
                          int max_drain_per_frame, int max_schedule_per_frame);
  MeshRebuildTickStats RebuildDirtyChunksWithStats(
      UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
      int max_schedule_per_frame, bool force_sync = false);
  void DrainAsyncMeshResults(UBlockWorld &world, UBlockRegistry &registry,
                             int max_per_frame);
  void RebuildChunkImmediate(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 chunk_coord);
  void WaitForAsyncMeshIdle();
  bool WaitForAsyncMeshIdleFor(std::chrono::milliseconds timeout);
  void CancelAsyncMeshWork();
  void CancelAsyncInFlightKeepDirty();

  bool HasPendingDirty() const;
  bool HasDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                      int radius_chunks) const;
  bool HasPendingAsyncMeshWork() const;
  size_t GetDirtyCount() const;
  int GetAsyncInFlightCount() const;
  uint64_t GetMeshDiscardedLateCount() const;
  double GetLastFlatRebuildMs() const;
  size_t GetGreedyCacheSize() const;
  bool HasGreedyMesh(glm::ivec3 chunk_coord) const;
  bool IsChunkMeshDirty(glm::ivec3 chunk_coord) const;
  uint64_t GetChunkMeshRevision(glm::ivec3 chunk_coord) const;
  bool HasInflightMeshBuild(glm::ivec3 chunk_coord) const;
  uint64_t GetInflightSourceRevision(glm::ivec3 chunk_coord) const;
  bool HasMissingGreedyMeshInHorizontalRadius(const UBlockWorld &world,
                                              glm::ivec3 center_ground_chunk,
                                              int radius_chunks) const;
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
      bool sync_neighbor_chunks = false);
  void MarkBlocksChunkDirtyBatchFromEdit(
      UBlockWorld &block_world, UBlockRegistry *registry,
      const std::vector<glm::ivec3> &block_positions,
      std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
      bool sync_neighbor_chunks = false);
  void MarkChunksContainingBlockIds(const UBlockWorld &block_world,
                                    const std::vector<BlockId> &block_ids);

  const std::vector<CrossInstanceBatch> &
  GetCrossRenderBatches(UBlockWorld &world, UBlockRegistry &registry,
                        const std::shared_ptr<UCamera> &camera);

private:
  void NotifyChunkBlocksChanged(glm::ivec3 chunk_coord);
  void NotifyChunkUnloaded(glm::ivec3 chunk_coord);

  UChunkMeshCache Cache;
  IUWorldMeshSink *MeshSink{nullptr};
};

} // namespace cutum

#endif
