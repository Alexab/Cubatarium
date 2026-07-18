#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <algorithm>
#include <iostream>
#include <thread>

namespace cutum
{

namespace
{
#ifndef NDEBUG
int gMeshTelemetryTick{0};
#endif
} // namespace

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::ComputeBudget(const ProceduralSettings &procedural,
                                       float movement_speed,
                                       int default_load_ops,
                                       double last_frame_ms) const
{
  FrameBudget budget;
  const bool boost =
      movement_speed > procedural.MovementSpeedBoostThreshold;
  budget.MaxChunkCommits =
      boost ? procedural.MaxChunkCommitsPerFrameBoost
            : procedural.MaxChunkCommitsPerFrame;
  budget.MaxLoadOps = boost ? procedural.MaxLoadOpsPerFrameBoost
                            : default_load_ops;
  budget.MaxMeshDrain = kDefaultMeshDrain;
  budget.MaxMeshSchedule = kDefaultMeshSchedule;
  if (last_frame_ms > 24.0)
  {
    // Hitch: cut commits/load, keep mesh drain so Dirty does not spiral.
    budget.MaxChunkCommits = 1;
    budget.MaxLoadOps = std::max(1, budget.MaxLoadOps / 2);
  }
  else if (last_frame_ms > 20.0 && boost)
  {
    budget.MaxChunkCommits = std::max(1, budget.MaxChunkCommits - 1);
    budget.MaxLoadOps = std::max(1, budget.MaxLoadOps - 1);
  }
  if (boost && last_frame_ms <= 20.0)
  {
    budget.MaxMeshDrain =
        std::max(budget.MaxMeshDrain, budget.MaxChunkCommits * 2);
    budget.MaxMeshSchedule = budget.MaxMeshDrain;
  }
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::WarmupBudget(int mesh_flush)
{
  FrameBudget budget;
  budget.MaxChunkCommits = mesh_flush;
  budget.MaxLoadOps = mesh_flush;
  budget.MaxMeshDrain = mesh_flush;
  budget.MaxMeshSchedule = mesh_flush;
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::CooperativeWarmupBudget(int coop_budget)
{
  const int mesh =
      std::min(128, std::max(coop_budget * 8, 32));
  FrameBudget budget;
  budget.MaxMeshDrain = mesh;
  budget.MaxMeshSchedule = mesh;
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::CreateMeshWarmupBudget(int coop_budget)
{
  const int threads = std::max(
      2, static_cast<int>(std::thread::hardware_concurrency() > 0
                              ? std::thread::hardware_concurrency()
                              : 4));
  const int mesh =
      std::min(512, std::max(coop_budget * 24, threads * 32));
  FrameBudget budget;
  budget.MaxMeshDrain = mesh;
  budget.MaxMeshSchedule = mesh;
  return budget;
}

void UChunkEmergeCoordinator::BeginFrame(const ProceduralSettings &procedural,
                                         float movement_speed,
                                         int default_load_ops,
                                         double last_frame_ms)
{
  LastBudget =
      ComputeBudget(procedural, movement_speed, default_load_ops, last_frame_ms);
}

void UChunkEmergeCoordinator::TickMeshEmerge(UWorld &world)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
  const ProceduralSettings &procedural = world.GetProceduralSettings();
  const float movement_speed = world.GetLastMovementSpeed();
  const bool moving =
      movement_speed > procedural.MovementSpeedBoostThreshold;
  const double last_frame_ms = world.GetLastMovementFrameMs();

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_ground_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetEffectiveRenderDistance() + 1;
  mesh_service.SetMeshRebuildFocus(focus_ground_horiz, focus_radius);
  mesh_service.SetDeferMeshUntilLitFn(
      [&world](glm::ivec3 chunk_coord)
      {
        return world.IsPendingLightBeforeMesh(
            glm::ivec2(chunk_coord.x, chunk_coord.z));
      });

  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;

  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();
  const bool missing_visible_mesh =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius);
  const bool near_mesh_backlog =
      mesh_service.HasDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                 focus_radius) ||
      missing_visible_mesh;
  const bool pending_near_light =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);

  if (moving && near_mesh_backlog)
  {
    if (pending_dirty > 48 || pending_async > 16)
    {
      mesh_drain = std::max(mesh_drain, 16);
      mesh_schedule = std::max(mesh_schedule, 16);
    }
    else if (pending_dirty > 16 || pending_async > 8)
    {
      mesh_drain = std::max(mesh_drain, 12);
      mesh_schedule = std::max(mesh_schedule, 12);
    }
  }

  // Standing still with backlog: prioritize drain/complete over new commits so
  // FPS can recover (dirty outside focus used to never clear).
  if (!moving && pending_dirty > 32)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 12);
  }
  else if (!moving && pending_dirty > 8)
  {
    mesh_drain = std::max(mesh_drain, 16);
    mesh_schedule = std::max(mesh_schedule, 10);
  }

  if (world.GetPlayerRelightMeshBurstFrames() > 0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }

  // Near dirty must keep MeshAsync draining even under hitch frames.
  if (near_mesh_backlog)
  {
    mesh_drain = std::max(mesh_drain, 12);
    mesh_schedule = std::max(mesh_schedule, 12);
  }

  // Floor drain by Dirty backlog so hitch frames do not starve MeshAsync.
  if (pending_dirty > 0)
  {
    const int dirty_floor =
        std::min(24, std::max(1, static_cast<int>(pending_dirty) / 4));
    mesh_drain = std::max(mesh_drain, dirty_floor);
    // Under hitch, still drain completed results, but limit new snapshots —
    // except when the focus ring still has holes or awaiting first light.
    if (last_frame_ms > 24.0 && !missing_visible_mesh && !pending_near_light)
    {
      mesh_drain = std::max(mesh_drain, 8);
      mesh_schedule = std::min(mesh_schedule, 4);
    }
    else
    {
      mesh_schedule = std::max(mesh_schedule, dirty_floor);
    }
  }

  // Remesh after light before consuming other dirty work so black (light=0)
  // meshes do not stick for many frames under ocean stream backlog.
  {
    const int flush_n =
        pending_dirty < 16 ? 64 : (pending_dirty < 48 ? 32 : 24);
    world.FlushPendingRelightMeshColumns(flush_n);
  }
  // Already-meshed focus columns with sky=0 never remesh unless relight is
  // re-queued (stuck black after premature light=0 mesh).
  world.RecoverUnlitFocusMeshes(moving ? 2 : 4);

  // Sync-fill holes where voxels exist but GreedyCache entry is missing.
  int sync_cap = last_frame_ms > 16.0 ? 1 : -1;
  if (pending_async > 0 && last_frame_ms > 24.0)
  {
    sync_cap = sync_cap < 0 ? 2 : std::min(sync_cap, 2);
  }
  if (missing_visible_mesh)
  {
    // Prefer filling visible holes (incl. water/transparent) over hitch gates.
    const int missing_cap = moving ? 6 : 8;
    if (pending_async > 0 && last_frame_ms > 28.0)
    {
      sync_cap = std::max(sync_cap, 3);
    }
    else
    {
      sync_cap = std::max(sync_cap, missing_cap);
    }
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 12 : 16);
  }
  else if (near_mesh_backlog)
  {
    sync_cap = std::max(sync_cap, pending_async > 0 && last_frame_ms > 24.0 ? 1
                                                                             : 2);
  }
  else if (!moving && pending_dirty > 64 && last_frame_ms <= 20.0)
  {
    sync_cap = 2;
  }
  constexpr double kSyncRebuildBudgetMs = 6.0;
  const MeshRebuildTickStats tick_stats = mesh_service.RebuildDirtyChunksWithStats(
      world.GetBlockWorld(), registry, mesh_drain, mesh_schedule,
      /*force_sync=*/false, sync_cap, kSyncRebuildBudgetMs);
  mesh_service.DrainAsyncMeshResults(world.GetBlockWorld(), registry, mesh_drain);

#ifndef NDEBUG
  ++gMeshTelemetryTick;
  if (gMeshTelemetryTick % 60 == 0)
  {
    std::cout << "[MeshEmerge] dirty=" << mesh_service.GetDirtyCount()
              << " inflight=" << mesh_service.GetAsyncInFlightCount()
              << " sync=" << tick_stats.SyncRebuilt
              << " completed=" << tick_stats.Completed
              << " scheduled=" << tick_stats.Scheduled << std::endl;
  }
#endif
}

} // namespace cutum
