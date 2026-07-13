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

void UWorldMeshService::MarkAllDirtyFromWorld(const UBlockWorld &world)
{
  Cache.MarkAllDirtyFromWorld(world);
}

void UWorldMeshService::RemoveChunk(glm::ivec3 chunk_coord)
{
  Cache.RemoveChunk(chunk_coord);
  NotifyChunkUnloaded(chunk_coord);
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

void UWorldMeshService::MarkTerrainChunkMeshDirty(glm::ivec3 ground_chunk_coord,
                                                  int min_y, int max_y)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  for (int cx = ground_chunk_coord.x - 1; cx <= ground_chunk_coord.x + 1; ++cx)
  {
    for (int cz = ground_chunk_coord.z - 1; cz <= ground_chunk_coord.z + 1;
         ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        MarkDirty(glm::ivec3(cx, cy, cz));
      }
    }
  }
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
    int max_schedule_per_frame, bool force_sync)
{
  return Cache.RebuildDirtyChunksWithStats(world, registry, max_drain_per_frame,
                                           max_schedule_per_frame, force_sync);
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

bool UWorldMeshService::HasPendingDirty() const
{
  return Cache.HasPendingDirty();
}

bool UWorldMeshService::HasDirtyWithinHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks) const
{
  return Cache.HasDirtyWithinHorizontalRadius(center_chunk, radius_chunks);
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

double UWorldMeshService::GetLastFlatRebuildMs() const
{
  return Cache.GetLastFlatRebuildMs();
}

size_t UWorldMeshService::GetGreedyCacheSize() const
{
  return Cache.GetGreedyCacheSize();
}

bool UWorldMeshService::HasGreedyMesh(glm::ivec3 chunk_coord) const
{
  return Cache.HasGreedyMesh(chunk_coord);
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
  return Cache.GetGreedyBatches();
}

UWorldMeshService::GreedyDrawSnapshot
UWorldMeshService::PrepareGreedyDraw(UBlockWorld &world,
                                     UBlockRegistry &registry,
                                     const std::shared_ptr<UCamera> &camera)
{
  PrepareFaceInstances(world, registry, camera);
  return GreedyDrawSnapshot{Cache.GetGreedyBatches(), Cache.GetCrossBatches(),
                            Cache.GetMeshRevision(), Cache.GetCullRevision()};
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
