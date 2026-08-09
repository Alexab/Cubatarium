#include "World/Mesh/WorldMeshService.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/Frustum.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/EditMeshRemeshPolicy.h"
#include "World/Physics/PhysicsTelemetry.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <unordered_set>
#include <vector>

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

void UWorldMeshService::SetOnLitPendingNeededFn(
    std::function<void(glm::ivec3)> fn)
{
  Cache.SetOnLitPendingNeededFn(std::move(fn));
}

void UWorldMeshService::SetOnSoftDeferHeldFn(std::function<void(glm::ivec3)> fn)
{
  Cache.SetOnSoftDeferHeldFn(std::move(fn));
}

void UWorldMeshService::SetStarveOutsideFocusMesh(bool starve)
{
  Cache.SetStarveOutsideFocusMesh(starve);
}

void UWorldMeshService::SetStarveRemeshForHoles(bool starve)
{
  Cache.SetStarveRemeshForHoles(starve);
}

void UWorldMeshService::SetStarveRemeshKeepHoriz(int keep_h)
{
  Cache.SetStarveRemeshKeepHoriz(keep_h);
}

void UWorldMeshService::SetMeshWorkAdmission(const MeshWorkAdmission &adm)
{
  Cache.SetMeshWorkAdmission(adm);
}

const MeshWorkAdmission &UWorldMeshService::GetMeshWorkAdmission() const
{
  return Cache.GetMeshWorkAdmission();
}

bool UWorldMeshService::TryConsumeDirtyAdmit()
{
  return Cache.TryConsumeDirtyAdmit();
}

int UWorldMeshService::DropRemeshDirtyBeyondRadius(glm::ivec3 center_chunk,
                                                   int keep_radius, int keep_cy,
                                                   bool remesh_only)
{
  return Cache.DropRemeshDirtyBeyondRadius(center_chunk, keep_radius, keep_cy,
                                           remesh_only);
}

void UWorldMeshService::SetSyncHoleFillRadius(int radius_chunks)
{
  Cache.SetSyncHoleFillRadius(radius_chunks);
}

void UWorldMeshService::SetMaxOutsideFocusMeshPerFrame(int count)
{
  Cache.SetMaxOutsideFocusMeshPerFrame(count);
}

void UWorldMeshService::SetMaxRearFocusMeshPerFrame(int count)
{
  Cache.SetMaxRearFocusMeshPerFrame(count);
}

void UWorldMeshService::SetMeshScheduleMaxHorizontalDist(int radius_chunks)
{
  Cache.SetMeshScheduleMaxHorizontalDist(radius_chunks);
}

void UWorldMeshService::SetMeshScheduleOverflowPerFrame(int count)
{
  Cache.SetMeshScheduleOverflowPerFrame(count);
}

void UWorldMeshService::SetMeshSnapshotBudgetMs(double ms)
{
  Cache.SetMeshSnapshotBudgetMs(ms);
}

void UWorldMeshService::SetMeshEmergeTotalBudgetMs(double ms)
{
  Cache.SetMeshEmergeTotalBudgetMs(ms);
}

double UWorldMeshService::GetMeshEmergeTotalBudgetMs() const
{
  return Cache.GetMeshEmergeTotalBudgetMs();
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

void UWorldMeshService::PrefetchMeshCapture(const UBlockWorld &world,
                                            glm::ivec3 chunk_coord)
{
  Cache.PrefetchMeshCapture(world, chunk_coord);
}

void UWorldMeshService::PrefetchMeshCaptureBand(const UBlockWorld &world,
                                                glm::ivec3 ground_chunk_coord,
                                                int min_y, int max_y)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    Cache.PrefetchMeshCapture(
        world, glm::ivec3(ground_chunk_coord.x, cy, ground_chunk_coord.z));
  }
}

void UWorldMeshService::RequestRemeshAfterApply(glm::ivec3 chunk_coord)
{
  Cache.RequestRemeshAfterApply(chunk_coord);
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

int UWorldMeshService::MarkMissingSlicesDirtyPriority(
    const UBlockWorld &world, glm::ivec3 ground_chunk_coord, int min_y,
    int max_y)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  int marked = 0;
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 coord(ground_chunk_coord.x, cy, ground_chunk_coord.z);
    const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
    if (!chunk || HasMeshSatisfyingColumnReady(coord) ||
        IsPendingGpuApply(coord) || HasInflightMeshBuild(coord))
    {
      continue;
    }
    bool solid = false;
    for (int z = 0; z < CHUNK_SIZE && !solid; z += 4)
    {
      for (int x = 0; x < CHUNK_SIZE && !solid; x += 4)
      {
        for (int y = 0; y < CHUNK_SIZE && !solid; y += 4)
        {
          if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
          {
            solid = true;
          }
        }
      }
    }
    if (!solid)
    {
      continue;
    }
    MarkDirtyPriority(coord);
    ++marked;
  }
  return marked;
}

int UWorldMeshService::EnqueueColumnMissingDigSeamBelow(
    const UBlockWorld &world, glm::ivec3 block_pos, int max_enqueue)
{
  if (max_enqueue <= 0)
  {
    return 0;
  }
  const int gx = FloorDiv(block_pos.x, CHUNK_SIZE);
  const int gz = FloorDiv(block_pos.z, CHUNK_SIZE);
  const int placed_cy = FloorDiv(block_pos.y, CHUNK_SIZE);
  int enqueued = 0;
  for (int cy = 0; cy < placed_cy && enqueued < max_enqueue; ++cy)
  {
    const glm::ivec3 coord(gx, cy, gz);
    const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
    if (!chunk || HasMeshSatisfyingColumnReady(coord) ||
        IsPendingGpuApply(coord) || HasInflightMeshBuild(coord))
    {
      continue;
    }
    bool solid = false;
    for (int z = 0; z < CHUNK_SIZE && !solid; z += 4)
    {
      for (int x = 0; x < CHUNK_SIZE && !solid; x += 4)
      {
        for (int y = 0; y < CHUNK_SIZE && !solid; y += 4)
        {
          if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
          {
            solid = true;
          }
        }
      }
    }
    if (!solid)
    {
      continue;
    }
    EnqueueDigSeam(coord);
    ++enqueued;
  }
  return enqueued;
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

void UWorldMeshService::UpdateStickyNearestHole(glm::ivec3 coord, bool alive)
{
  if (!alive)
  {
    StickyNearestHoleFrames = 0;
    StickyNearestHoleCoord = glm::ivec3(0);
    return;
  }
  if (coord == StickyNearestHoleCoord)
  {
    StickyNearestHoleFrames =
        std::min(StickyNearestHoleFrames + 1, 1000000);
  }
  else
  {
    StickyNearestHoleCoord = coord;
    StickyNearestHoleFrames = 1;
  }
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
    double max_sync_ms, bool skip_gpu_consume)
{
  return Cache.RebuildDirtyChunksWithStats(world, registry, max_drain_per_frame,
                                           max_schedule_per_frame, force_sync,
                                           max_sync_rebuild, max_sync_ms,
                                           skip_gpu_consume);
}

int UWorldMeshService::ConsumeGpuApplyBacklog(UBlockWorld &world,
                                              UBlockRegistry &registry,
                                              int max_drain, int gpu_max,
                                              double gpu_budget_ms)
{
  return Cache.ConsumeGpuApplyBacklog(world, registry, max_drain, gpu_max,
                                      gpu_budget_ms);
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

void UWorldMeshService::InvalidateEditMeshNeighborhood(
    const std::vector<glm::ivec3> &block_positions)
{
  std::unordered_set<glm::ivec3, IVec3Hash> chunks;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    chunks.insert(UChunkManager::WorldToChunk(block_pos));
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      chunks.insert(UChunkManager::WorldToChunk(block_pos + offset));
    }
  }
  for (const glm::ivec3 &chunk_coord : chunks)
  {
    Cache.InvalidateInFlightMeshBuild(chunk_coord);
  }
}

void UWorldMeshService::ResetImmediateMeshStats()
{
  Cache.ResetImmediateMeshStats();
}

void UWorldMeshService::BeginHoleQueryFrame()
{
  Cache.BeginHoleQueryFrame();
}

double UWorldMeshService::GetLastMeshImmediateMs() const
{
  return Cache.GetLastMeshImmediateMs();
}

int UWorldMeshService::GetLastMeshImmediateCount() const
{
  return Cache.GetLastMeshImmediateCount();
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

int UWorldMeshService::CountDirtyWithinHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks) const
{
  return Cache.CountDirtyWithinHorizontalRadius(center_chunk, radius_chunks);
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

void UWorldMeshService::ReserveDirtyCapacity(size_t n)
{
  Cache.ReserveDirtyCapacity(n);
}

int UWorldMeshService::MaybeDropFarthestDirty(glm::ivec3 focus_ground_chunk,
                                              size_t soft_cap,
                                              int min_keep_horiz)
{
  return Cache.MaybeDropFarthestDirty(focus_ground_chunk, soft_cap,
                                      min_keep_horiz);
}

int UWorldMeshService::GetAsyncInFlightCount() const
{
  return Cache.GetAsyncInFlightCount();
}

size_t UWorldMeshService::GetMeshCompletedSize() const
{
  return Cache.GetMeshCompletedSize();
}

size_t UWorldMeshService::GetMeshCompletedCapacity() const
{
  return Cache.GetMeshCompletedCapacity();
}

uint64_t UWorldMeshService::GetMeshCompletedDiscardedOverflow() const
{
  return Cache.GetMeshCompletedDiscardedOverflow();
}

void UWorldMeshService::SetMeshCompletedCapacity(size_t cap)
{
  Cache.SetMeshCompletedCapacity(cap);
}

uint64_t UWorldMeshService::GetMeshDiscardedLateCount() const
{
  return Cache.GetMeshDiscardedLateCount();
}

uint64_t UWorldMeshService::GetMeshApplyStaleCount() const
{
  return Cache.GetMeshApplyStaleCount();
}

uint64_t UWorldMeshService::GetMeshReplaceHoleAvoidedCount() const
{
  return Cache.GetMeshReplaceHoleAvoidedCount();
}

uint64_t UWorldMeshService::GetSoftDeferEmptyPublishAvoidedCount() const
{
  return Cache.GetSoftDeferEmptyPublishAvoidedCount();
}

size_t UWorldMeshService::GetPendingGpuAppliesCount() const
{
  return Cache.GetPendingGpuAppliesCount();
}

size_t UWorldMeshService::GetPendingGpuQueuedCount() const
{
  return Cache.GetPendingGpuQueuedCount();
}

size_t UWorldMeshService::GetPendingGpuKickedCount() const
{
  return Cache.GetPendingGpuKickedCount();
}

int UWorldMeshService::GetLastGpuKickN() const
{
  return Cache.GetLastGpuKickN();
}

int UWorldMeshService::GetLastGpuFinishN() const
{
  return Cache.GetLastGpuFinishN();
}

int UWorldMeshService::GetLastGpuFinishNotReadyN() const
{
  return Cache.GetLastGpuFinishNotReadyN();
}

int UWorldMeshService::CountPendingGpuAppliesInHorizontalRadius(
    glm::ivec3 center_ground_chunk, int radius_chunks) const
{
  return Cache.CountPendingGpuAppliesInHorizontalRadius(center_ground_chunk,
                                                      radius_chunks);
}

int UWorldMeshService::DrainPendingGpuMeshes(UBlockWorld &world,
                                             UBlockRegistry &registry,
                                             int max_count, double budget_ms)
{
  return Cache.DrainPendingGpuMeshes(world, registry, max_count, budget_ms);
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

double UWorldMeshService::GetLastMeshDirtyTickMs() const
{
  return Cache.GetLastMeshDirtyTickMs();
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

bool UWorldMeshService::HasMeshSatisfyingColumnReady(glm::ivec3 chunk_coord) const
{
  return Cache.HasMeshSatisfyingColumnReady(chunk_coord);
}

size_t UWorldMeshService::GetSoftDeferHeldCount() const
{
  return Cache.GetSoftDeferHeldCount();
}

bool UWorldMeshService::IsSoftDeferHeld(glm::ivec3 chunk_coord) const
{
  return Cache.IsSoftDeferHeld(chunk_coord);
}

bool UWorldMeshService::HasSoftDeferHeldInColumn(glm::ivec2 ground_xz) const
{
  return Cache.HasSoftDeferHeldInColumn(ground_xz);
}

bool UWorldMeshService::IsGpuExtractInFlight(glm::ivec3 chunk_coord) const
{
  return Cache.IsGpuExtractInFlight(chunk_coord);
}

bool UWorldMeshService::IsPendingGpuApply(glm::ivec3 chunk_coord) const
{
  return Cache.IsPendingGpuApply(chunk_coord);
}

bool UWorldMeshService::IsPendingGpuQueued(glm::ivec3 chunk_coord) const
{
  return Cache.IsPendingGpuQueued(chunk_coord);
}

bool UWorldMeshService::IsPendingGpuKickedOrDispatched(glm::ivec3 chunk_coord) const
{
  return Cache.IsPendingGpuKickedOrDispatched(chunk_coord);
}

bool UWorldMeshService::PreferKickPendingGpuQueued(glm::ivec3 chunk_coord)
{
  return Cache.PreferKickPendingGpuQueued(chunk_coord);
}

bool UWorldMeshService::DropQueuedPendingGpuApply(glm::ivec3 chunk_coord)
{
  return Cache.DropQueuedPendingGpuApply(chunk_coord);
}

bool UWorldMeshService::ChunkHasStaleDarkFaces(glm::ivec3 chunk_coord,
                                              const UBlockWorld &world) const
{
  return Cache.ChunkHasStaleDarkFaces(chunk_coord, world);
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
    bool sync_neighbor_chunks, bool sync_light_ring)
{
  MarkBlocksChunkDirtyBatchFromEdit(block_world, registry, {block_pos},
                                    modified_chunks, sync_neighbor_chunks,
                                    sync_light_ring, false, nullptr);
}

void UWorldMeshService::MarkBlocksChunkDirtyBatchFromEdit(
    UBlockWorld &block_world, UBlockRegistry *registry,
    const std::vector<glm::ivec3> &block_positions,
    std::unordered_set<glm::ivec3, IVec3Hash> &modified_chunks,
    bool sync_neighbor_chunks, bool sync_light_ring, bool collect_break_diag,
    PhysicsTelemetry *break_tele)
{
  if (block_positions.empty())
  {
    return;
  }

  EditMeshRemeshInput policy_in;
  policy_in.BlockPositions = block_positions;
  policy_in.SyncNeighborChunks = sync_neighbor_chunks;
  policy_in.SyncLightRing = sync_light_ring;
  policy_in.HasRegistry = registry != nullptr;
  const RenderSettings &render = Cache.GetRenderSettings();
  policy_in.AsyncMeshing = render.AsyncMeshing;
  policy_in.GreedyMeshing = render.GreedyMeshing;
  policy_in.ImmediateChunkCap = 2;
  policy_in.PreferGpuStorePatch = PreferGpuStorePatch;

  const EditMeshRemeshDecision decision = EvaluateEditMeshRemesh(policy_in);
  LastEditImmediateN = decision.ImmediateChunks.size();
  LastEditDirtyN = decision.DirtyChunks.size();
  modified_chunks.insert(decision.ImmediateChunks.begin(),
                         decision.ImmediateChunks.end());
  modified_chunks.insert(decision.DirtyChunks.begin(),
                         decision.DirtyChunks.end());
  if (break_tele)
  {
    break_tele->EditImmediateN = LastEditImmediateN;
    break_tele->EditDirtyN = LastEditDirtyN;
  }

  if (!registry)
  {
    for (const glm::ivec3 &chunk_coord : decision.DirtyChunks)
    {
      MarkDirtyPriority(chunk_coord);
    }
    return;
  }

  auto note_race_before_immediate = [&](glm::ivec3 chunk_coord)
  {
    if (collect_break_diag && break_tele && HasInflightMeshBuild(chunk_coord))
    {
      ++break_tele->BreakInflightRaceN;
    }
  };
  auto note_dark_after_immediate = [&](glm::ivec3 chunk_coord)
  {
    if (collect_break_diag && break_tele &&
        Cache.ChunkHasFullyDarkFace(chunk_coord))
    {
      ++break_tele->BreakDarkFaceN;
    }
  };

  // C1/P2: hard time-cap Immediate remesh — fluid/edit with cap=9 burned
  // physics_block ~0.8–1.4s (manual 162944). Dig bursts (edit_immediate_n=7,
  // manual 191432 spikes 700–860ms) must not chain more than one Immediate
  // on the hot frame; defer remainder to Dirty/async + DigSeam for face X-ray.
  std::unordered_set<glm::ivec3, IVec3Hash> center_chunks;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    center_chunks.insert(detail::EditWorldToChunk(block_pos));
  }
  auto is_face_adj = [&](const glm::ivec3 &chunk_coord) -> bool
  {
    for (const glm::ivec3 &c : center_chunks)
    {
      const int dx = std::abs(chunk_coord.x - c.x);
      const int dy = std::abs(chunk_coord.y - c.y);
      const int dz = std::abs(chunk_coord.z - c.z);
      if (dx + dy + dz == 1)
      {
        return true;
      }
    }
    return false;
  };

  const auto imm_budget_t0 = std::chrono::high_resolution_clock::now();
  constexpr double kEditImmediateBudgetMs = 8.0;
  constexpr int kEditImmediateMaxHot = 1;
  int immediate_done = 0;
  for (const glm::ivec3 &chunk_coord : decision.ImmediateChunks)
  {
    const double spent_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - imm_budget_t0)
            .count();
    if (immediate_done >= kEditImmediateMaxHot ||
        (immediate_done > 0 && spent_ms > kEditImmediateBudgetMs))
    {
      MarkDirtyPriority(chunk_coord);
      // Face Immediate demoted by P2 → DigSeam (manual 215711 side X-ray).
      if (is_face_adj(chunk_coord) || center_chunks.count(chunk_coord) == 0)
      {
        EnqueueDigSeam(chunk_coord);
      }
      continue;
    }
    note_race_before_immediate(chunk_coord);
    RebuildChunkImmediate(block_world, *registry, chunk_coord);
    note_dark_after_immediate(chunk_coord);
    ++immediate_done;
  }
  for (const glm::ivec3 &chunk_coord : decision.DirtyChunks)
  {
    MarkDirtyPriority(chunk_coord);
  }
}

void UWorldMeshService::EnqueueDigSeam(glm::ivec3 chunk_coord)
{
  DigSeam.Enqueue(chunk_coord);
}

void UWorldMeshService::TickDigSeamDrain(UBlockWorld &block_world,
                                         UBlockRegistry &registry,
                                         const PhysicsTelemetry *frame_tele)
{
  LastDigSeamRemeshN = 0;
  LastDigSeamPendingN = static_cast<int>(DigSeam.Size());
  if (DigSeam.Empty())
  {
    return;
  }
  // Never stack a second Immediate on the dig/edit hot frame.
  if (GetLastMeshImmediateCount() > 0)
  {
    return;
  }
  if (frame_tele &&
      (frame_tele->BreakCompleteN > 0 || frame_tele->PlaceCompleteN > 0))
  {
    return;
  }

  while (!DigSeam.Empty())
  {
    glm::ivec3 coord;
    if (!DigSeam.TryPop(coord))
    {
      break;
    }
    LastDigSeamPendingN = static_cast<int>(DigSeam.Size());

    // Era23 I-P1: !drawable underfeet/near is a hole — remesh Immediate, do not
    // treat SoftDefer empty as "already ok".
    RebuildChunkImmediate(block_world, registry, coord);
    LastDigSeamRemeshN = 1;
    break;
  }
}

} // namespace cutum
