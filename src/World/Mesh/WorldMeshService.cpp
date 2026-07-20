#include "World/Mesh/WorldMeshService.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/Frustum.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include <unordered_set>

namespace cutum
{

UWorldMeshService::UWorldMeshService() = default;

void UWorldMeshService::SetRenderSettings(const RenderSettings &settings)
{
  Cache.SetRenderSettings(settings);
}

void UWorldMeshService::SetRenderDistanceChunks(int distance)
{
  Cache.SetRenderDistanceChunks(distance);
}

void UWorldMeshService::SetMeshRebuildFocus(glm::ivec3 ground_chunk_coord,
                                            int radius_chunks)
{
  Cache.SetMeshRebuildFocus(ground_chunk_coord, radius_chunks);
}

void UWorldMeshService::SetMeshVerticalPriority(int preferred_cy,
                                                bool prefer_lower_cy)
{
  Cache.SetMeshVerticalPriority(preferred_cy, prefer_lower_cy);
}

void UWorldMeshService::ClearMeshVerticalPriority()
{
  Cache.ClearMeshVerticalPriority();
}

void UWorldMeshService::SetMeshForwardBias(float bias_k, glm::vec2 forward_xz)
{
  Cache.SetMeshForwardBias(bias_k, forward_xz);
}

void UWorldMeshService::SetDeferMeshUntilLitFn(std::function<bool(glm::ivec3)> fn)
{
  Cache.SetDeferMeshUntilLitFn(std::move(fn));
}

void UWorldMeshService::SetStarveOutsideFocusMesh(bool starve)
{
  Cache.SetStarveOutsideFocusMesh(starve);
}

void UWorldMeshService::SetStarveRemeshForHoles(bool starve)
{
  Cache.SetStarveRemeshForHoles(starve);
}

void UWorldMeshService::SetMaxOutsideFocusMeshPerFrame(int count)
{
  Cache.SetMaxOutsideFocusMeshPerFrame(count);
}

void UWorldMeshService::SetMeshScheduleMaxHorizontalDist(int radius_chunks)
{
  Cache.SetMeshScheduleMaxHorizontalDist(radius_chunks);
}

void UWorldMeshService::SetMeshScheduleOverflowPerFrame(int count)
{
  Cache.SetMeshScheduleOverflowPerFrame(count);
}

void UWorldMeshService::SetAltitudeCullState(float altitude_above_terrain,
                                             int threshold_blocks)
{
  Cache.SetAltitudeCullState(altitude_above_terrain, threshold_blocks);
}

void UWorldMeshService::NotifyChunkBlocksChanged(glm::ivec3 chunk_coord)
{
  if (MeshSink)
  {
    MeshSink->OnChunkBlocksChanged(chunk_coord);
  }
}

void UWorldMeshService::NotifyChunkUnloaded(glm::ivec3 chunk_coord)
{
  if (MeshSink)
  {
    MeshSink->OnChunkUnloaded(chunk_coord);
  }
}

void UWorldMeshService::MarkDirty(glm::ivec3 chunk_coord)
{
  Cache.MarkDirty(chunk_coord);
  NotifyChunkBlocksChanged(chunk_coord);
}

void UWorldMeshService::MarkDirtyPriority(glm::ivec3 chunk_coord)
{
  Cache.MarkDirtyPriority(chunk_coord);
  NotifyChunkBlocksChanged(chunk_coord);
}

void UWorldMeshService::NotifyFluidSurfaceDirtyAtBlock(
    const UBlockWorld &world, UBlockRegistry *registry, glm::ivec3 block_pos)
{
  if (!registry)
  {
    return;
  }
  auto touches_liquid = [&](glm::ivec3 p)
  { return registry->IsLiquid(world.GetBlock(p)); };
  if (!touches_liquid(block_pos))
  {
    bool neighbor_liquid = false;
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      if (touches_liquid(block_pos + offset))
      {
        neighbor_liquid = true;
        break;
      }
    }
    if (!neighbor_liquid)
    {
      return;
    }
  }
  Cache.InvalidateFluidSurfaceForChunk(UChunkManager::WorldToChunk(block_pos));
}

void UWorldMeshService::InvalidateFluidSurfaceForColumn(
    glm::ivec3 ground_chunk_coord, bool include_neighbors)
{
  Cache.InvalidateFluidSurfaceForColumn(ground_chunk_coord, include_neighbors);
}

void UWorldMeshService::MarkAllDirtyFromWorld(const UBlockWorld &world)
{
  Cache.MarkAllDirtyFromWorld(world);
}

void UWorldMeshService::RemoveChunk(glm::ivec3 chunk_coord)
{
  Cache.RemoveChunk(chunk_coord);
  NotifyChunkUnloaded(chunk_coord);
}

void UWorldMeshService::RemoveColumn(glm::ivec3 ground_coord, int max_cy)
{
  Cache.RemoveColumn(ground_coord, max_cy);
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  max_cy = std::max(0, max_cy);
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    NotifyChunkUnloaded(glm::ivec3(ground_coord.x, cy, ground_coord.z));
  }
}

void UWorldMeshService::MarkColumnMeshDirty(int world_x, int world_z, int min_y,
                                            int max_y)
{
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, min_y, world_z));
  const glm::ivec3 top =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, max_y, world_z));
  std::unordered_set<glm::ivec3, IVec3Hash> dirty_chunks;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int cy = base.y; cy <= top.y; ++cy)
      {
        dirty_chunks.insert(glm::ivec3(base.x + dx, cy, base.z + dz));
      }
    }
  }
  for (const glm::ivec3 &coord : dirty_chunks)
  {
    MarkDirty(coord);
  }
}

void UWorldMeshService::MarkTerrainChunkMeshDirtySeamed(
    glm::ivec3 ground_chunk_coord, int min_y, int max_y,
    bool include_horizontal_neighbors)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  const int cx0 = include_horizontal_neighbors ? ground_chunk_coord.x - 1
                                               : ground_chunk_coord.x;
  const int cx1 = include_horizontal_neighbors ? ground_chunk_coord.x + 1
                                               : ground_chunk_coord.x;
  const int cz0 = include_horizontal_neighbors ? ground_chunk_coord.z - 1
                                               : ground_chunk_coord.z;
  const int cz1 = include_horizontal_neighbors ? ground_chunk_coord.z + 1
                                               : ground_chunk_coord.z;
  for (int cx = cx0; cx <= cx1; ++cx)
  {
    for (int cz = cz0; cz <= cz1; ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        MarkDirty(glm::ivec3(cx, cy, cz));
      }
    }
  }
}

void UWorldMeshService::MarkTerrainChunkMeshDirty(glm::ivec3 ground_chunk_coord,
                                                  int min_y, int max_y)
{
  MarkTerrainChunkMeshDirtySeamed(ground_chunk_coord, min_y, max_y, true);
}

void UWorldMeshService::MarkTerrainChunkMeshDirtySeamedPriority(
    glm::ivec3 ground_chunk_coord, int min_y, int max_y,
    bool include_horizontal_neighbors)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  const int cx0 = include_horizontal_neighbors ? ground_chunk_coord.x - 1
                                               : ground_chunk_coord.x;
  const int cx1 = include_horizontal_neighbors ? ground_chunk_coord.x + 1
                                               : ground_chunk_coord.x;
  const int cz0 = include_horizontal_neighbors ? ground_chunk_coord.z - 1
                                               : ground_chunk_coord.z;
  const int cz1 = include_horizontal_neighbors ? ground_chunk_coord.z + 1
                                               : ground_chunk_coord.z;
  for (int cx = cx0; cx <= cx1; ++cx)
  {
    for (int cz = cz0; cz <= cz1; ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        MarkDirtyPriority(glm::ivec3(cx, cy, cz));
      }
    }
  }
}

void UWorldMeshService::MarkTerrainChunkMeshDirtyPriority(
    glm::ivec3 ground_chunk_coord, int min_y, int max_y)
{
  MarkTerrainChunkMeshDirtySeamedPriority(ground_chunk_coord, min_y, max_y,
                                          true);
}

bool UWorldMeshService::HasMissingGreedyMeshInHorizontalRadius(
    const UBlockWorld &world, glm::ivec3 center_ground_chunk,
    int radius_chunks) const
{
  return Cache.HasMissingGreedyMeshInHorizontalRadius(world, center_ground_chunk,
                                                      radius_chunks);
}

bool UWorldMeshService::FindNearestMissingGreedyMesh(
    const UBlockWorld &world, glm::ivec3 center_ground_chunk, int radius_chunks,
    glm::ivec3 &out_coord) const
{
  return Cache.FindNearestMissingGreedyMesh(world, center_ground_chunk,
                                            radius_chunks, out_coord);
}

const MeshRebuildTickStats &UWorldMeshService::GetLastRebuildTickStats() const
{
  return Cache.GetLastRebuildTickStats();
}

void UWorldMeshService::RebuildAll(UBlockWorld &world, UBlockRegistry &registry)
{
  Cache.RebuildAll(world, registry);
}

void UWorldMeshService::RebuildDirtyChunks(UBlockWorld &world,
                                           UBlockRegistry &registry,
                                           int max_drain_per_frame,
                                           int max_schedule_per_frame)
{
  Cache.RebuildDirtyChunks(world, registry, max_drain_per_frame,
                           max_schedule_per_frame);
}

MeshRebuildTickStats UWorldMeshService::RebuildDirtyChunksWithStats(
    UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
    int max_schedule_per_frame, bool force_sync, int max_sync_rebuild,
    double max_sync_ms)
{
  return Cache.RebuildDirtyChunksWithStats(world, registry, max_drain_per_frame,
                                           max_schedule_per_frame, force_sync,
                                           max_sync_rebuild, max_sync_ms);
}

void UWorldMeshService::DrainAsyncMeshResults(UBlockWorld &world,
                                              UBlockRegistry &registry,
                                              int max_per_frame)
{
  Cache.DrainAsyncMeshResults(world, registry, max_per_frame);
}

void UWorldMeshService::RebuildChunkImmediate(const UBlockWorld &world,
                                              UBlockRegistry &registry,
                                              glm::ivec3 chunk_coord)
{
  Cache.RebuildChunkImmediate(world, registry, chunk_coord);
}

void UWorldMeshService::WaitForAsyncMeshIdle() { Cache.WaitForAsyncMeshIdle(); }

bool UWorldMeshService::WaitForAsyncMeshIdleFor(
    const std::chrono::milliseconds timeout)
{
  return Cache.WaitForAsyncMeshIdleFor(timeout);
}

void UWorldMeshService::CancelAsyncMeshWork() { Cache.CancelAsyncMeshWork(); }

void UWorldMeshService::CancelAsyncInFlightKeepDirty()
{
  Cache.CancelAsyncInFlightKeepDirty();
}

void UWorldMeshService::CancelInFlightOutsideHorizontalRadius(
    glm::ivec3 focus_ground_chunk, int radius_chunks)
{
  Cache.CancelInFlightOutsideHorizontalRadius(focus_ground_chunk, radius_chunks);
}

bool UWorldMeshService::HasPendingDirty() const
{
  return Cache.HasPendingDirty();
}

bool UWorldMeshService::HasDirtyWithinHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks) const
{
  return Cache.HasDirtyWithinHorizontalRadius(center_chunk, radius_chunks);
}

bool UWorldMeshService::HasDirtyInColumnBand(glm::ivec2 ground_xz, int min_y,
                                             int max_y) const
{
  return Cache.HasDirtyInColumnBand(ground_xz, min_y, max_y);
}

bool UWorldMeshService::HasPendingAsyncMeshWork() const
{
  return Cache.HasPendingAsyncMeshWork();
}

size_t UWorldMeshService::GetDirtyCount() const
{
  return Cache.GetDirtyCount();
}

int UWorldMeshService::GetAsyncInFlightCount() const
{
  return Cache.GetAsyncInFlightCount();
}

uint64_t UWorldMeshService::GetMeshDiscardedLateCount() const
{
  return Cache.GetMeshDiscardedLateCount();
}

double UWorldMeshService::GetLastFlatRebuildMs() const
{
  return Cache.GetLastFlatRebuildMs();
}

double UWorldMeshService::GetLastMeshSyncMs() const
{
  return Cache.GetLastMeshSyncMs();
}

double UWorldMeshService::GetLastMeshSnapshotMs() const
{
  return Cache.GetLastMeshSnapshotMs();
}

size_t UWorldMeshService::GetGreedyCacheSize() const
{
  return Cache.GetGreedyCacheSize();
}

bool UWorldMeshService::HasGreedyMesh(glm::ivec3 chunk_coord) const
{
  return Cache.HasGreedyMesh(chunk_coord);
}

bool UWorldMeshService::HasDrawableGreedyMesh(glm::ivec3 chunk_coord) const
{
  return Cache.HasDrawableGreedyMesh(chunk_coord);
}

size_t UWorldMeshService::GetGreedyVertexCount(glm::ivec3 chunk_coord) const
{
  return Cache.GetGreedyVertexCount(chunk_coord);
}

bool UWorldMeshService::IsChunkMeshDirty(glm::ivec3 chunk_coord) const
{
  return Cache.IsChunkMeshDirty(chunk_coord);
}

uint64_t UWorldMeshService::GetChunkMeshRevision(glm::ivec3 chunk_coord) const
{
  return Cache.GetChunkMeshRevision(chunk_coord);
}

bool UWorldMeshService::HasInflightMeshBuild(glm::ivec3 chunk_coord) const
{
  return Cache.HasInflightMeshBuild(chunk_coord);
}

uint64_t UWorldMeshService::GetInflightSourceRevision(
    glm::ivec3 chunk_coord) const
{
  return Cache.GetInflightSourceRevision(chunk_coord);
}

uint64_t UWorldMeshService::GetMeshRevision() const
{
  return Cache.GetMeshRevision();
}

uint64_t UWorldMeshService::GetCullRevision() const
{
  return Cache.GetCullRevision();
}

size_t UWorldMeshService::GetGreedyVertexCount() const
{
  return Cache.GetGreedyVertexCount();
}

size_t UWorldMeshService::GetInstanceCount() const
{
  return Cache.GetInstanceCount();
}

void UWorldMeshService::UpdateVisibleInstances(const Frustum &frustum,
                                               const glm::mat4 &view_proj,
                                               const glm::vec3 &camera_pos)
{
  Cache.UpdateVisibleInstances(frustum, view_proj, camera_pos);
}

void UWorldMeshService::WarmupVisibleListFromViewProj(
    const glm::mat4 &view_proj, const glm::vec3 &camera_pos)
{
  UpdateVisibleInstances(Frustum::FromViewProjection(view_proj), view_proj,
                         camera_pos);
}

const std::vector<FaceInstance> &UWorldMeshService::PrepareFaceInstances(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::shared_ptr<UCamera> &camera, int max_drain_per_frame,
    int max_schedule_per_frame)
{
  (void)world;
  (void)registry;
  (void)max_drain_per_frame;
  (void)max_schedule_per_frame;
  if (camera)
  {
    const glm::mat4 view = camera->GetViewMatrix();
    const glm::mat4 proj = camera->GetProjection();
    const glm::mat4 vp = proj * view;
    Cache.UpdateVisibleInstances(Frustum::FromViewProjection(vp), vp,
                                 camera->GetPosition());
  }
  return Cache.GetFaceInstances();
}

const std::vector<GreedyMeshBatch> &UWorldMeshService::GetGreedyRenderBatches(
    UBlockWorld &world, UBlockRegistry &registry,
    const std::shared_ptr<UCamera> &camera)
{
  PrepareFaceInstances(world, registry, camera);
  // Legacy API retained only for older call sites; prefer PrepareGreedyDraw.
  static const std::vector<GreedyMeshBatch> kEmpty;
  (void)world;
  (void)registry;
  (void)camera;
  return kEmpty;
}

UWorldMeshService::GreedyDrawSnapshot
UWorldMeshService::PrepareGreedyDraw(UBlockWorld &world,
                                     UBlockRegistry &registry,
                                     const std::shared_ptr<UCamera> &camera)
{
  PrepareFaceInstances(world, registry, camera);
  return GreedyDrawSnapshot{Cache,
                            Cache.GetGreedyOpaqueCutoutRefs(),
                            Cache.GetGreedyTransparentRefs(),
                            Cache.GetCrossBatches(),
                            Cache.GetMeshRevision(),
                            Cache.GetCullRevision()};
}

const std::vector<CrossInstanceBatch> &
UWorldMeshService::GetCrossRenderBatches(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         const std::shared_ptr<UCamera> &camera)
{
  PrepareFaceInstances(world, registry, camera);
  return Cache.GetCrossBatches();
}

void UWorldMeshService::MarkChunksContainingBlockIds(
    const UBlockWorld &block_world, const std::vector<BlockId> &block_ids)
{
  if (block_ids.empty())
  {
    MarkAllDirtyFromWorld(block_world);
    return;
  }
  std::unordered_set<BlockId> targets(block_ids.begin(), block_ids.end());
  block_world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        bool contains_target = false;
        for (int local_y = 0; local_y < CHUNK_SIZE && !contains_target;
             ++local_y)
        {
          for (int local_z = 0; local_z < CHUNK_SIZE && !contains_target;
               ++local_z)
          {
            for (int local_x = 0; local_x < CHUNK_SIZE && !contains_target;
                 ++local_x)
            {
              if (targets.count(chunk.GetBlockLocal(
                      glm::ivec3(local_x, local_y, local_z))) != 0)
              {
                contains_target = true;
              }
            }
          }
        }
        if (contains_target)
        {
          MarkDirty(chunk.GetCoord());
        }
      });
}

void UWorldMeshService::MarkBlockChunkDirtyFromEdit(
    UBlockWorld &block_world, UBlockRegistry *registry, glm::ivec3 block_pos,
    std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
    bool sync_neighbor_chunks)
{
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(block_pos);
  modified_chunks.insert(chunk_coord);

  const RenderSettings &render = Cache.GetRenderSettings();
  const bool full_sync_rebuild =
      registry != nullptr && (!render.AsyncMeshing || !render.GreedyMeshing);
  const bool hybrid_async_edit =
      registry != nullptr && render.AsyncMeshing && render.GreedyMeshing;

  if (full_sync_rebuild || (hybrid_async_edit && sync_neighbor_chunks))
  {
    RebuildChunkImmediate(block_world, *registry, chunk_coord);
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      RebuildChunkImmediate(block_world, *registry,
                            UChunkManager::WorldToChunk(block_pos + offset));
    }
    return;
  }

  if (hybrid_async_edit)
  {
    RebuildChunkImmediate(block_world, *registry, chunk_coord);
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      MarkDirtyPriority(UChunkManager::WorldToChunk(block_pos + offset));
    }
    return;
  }

  MarkDirtyPriority(chunk_coord);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    MarkDirtyPriority(UChunkManager::WorldToChunk(block_pos + offset));
  }
}

void UWorldMeshService::MarkBlocksChunkDirtyBatchFromEdit(
    UBlockWorld &block_world, UBlockRegistry *registry,
    const std::vector<glm::ivec3> &block_positions,
    std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
    bool sync_neighbor_chunks)
{
  if (block_positions.empty())
  {
    return;
  }
  std::unordered_set<glm::ivec3, IVec3Hash> chunk_coords;
  std::unordered_set<glm::ivec3, IVec3Hash> center_chunks;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    center_chunks.insert(UChunkManager::WorldToChunk(block_pos));
    chunk_coords.insert(UChunkManager::WorldToChunk(block_pos));
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      chunk_coords.insert(UChunkManager::WorldToChunk(block_pos + offset));
    }
  }
  modified_chunks.insert(chunk_coords.begin(), chunk_coords.end());
  if (!registry)
  {
    for (const glm::ivec3 &chunk_coord : chunk_coords)
    {
      MarkDirtyPriority(chunk_coord);
    }
    return;
  }
  const RenderSettings &render = Cache.GetRenderSettings();
  const bool full_sync_rebuild =
      !render.AsyncMeshing || !render.GreedyMeshing;
  const bool hybrid_async_edit =
      registry != nullptr && render.AsyncMeshing && render.GreedyMeshing;
  if (full_sync_rebuild || (hybrid_async_edit && sync_neighbor_chunks))
  {
    for (const glm::ivec3 &chunk_coord : chunk_coords)
    {
      RebuildChunkImmediate(block_world, *registry, chunk_coord);
    }
    return;
  }

  for (const glm::ivec3 &chunk_coord : chunk_coords)
  {
    if (center_chunks.count(chunk_coord))
    {
      RebuildChunkImmediate(block_world, *registry, chunk_coord);
    }
    else
    {
      MarkDirtyPriority(chunk_coord);
    }
  }
}

} // namespace cutum
