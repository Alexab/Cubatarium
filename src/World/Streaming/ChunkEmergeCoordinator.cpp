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
  if (boost)
  {
    budget.MaxMeshDrain =
        std::max(budget.MaxMeshDrain, budget.MaxChunkCommits * 2);
    budget.MaxMeshSchedule = budget.MaxMeshDrain;
  }
  if (last_frame_ms > 24.0)
  {
    budget.MaxChunkCommits = std::max(1, budget.MaxChunkCommits / 2);
    budget.MaxLoadOps = std::max(1, budget.MaxLoadOps / 2);
  }
  else if (last_frame_ms > 16.0)
  {
    budget.MaxMeshDrain = std::max(4, budget.MaxMeshDrain - 2);
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
  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_ground_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetRenderDistanceChunks() + 1;
  mesh_service.SetMeshRebuildFocus(focus_ground_horiz, focus_radius);

  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;
  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();
  const bool near_mesh_backlog =
      mesh_service.HasDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                 focus_radius) ||
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(world.GetBlockWorld(),
                                                         focus_ground_horiz,
                                                         focus_radius);
  if (near_mesh_backlog)
  {
    mesh_drain = std::max(mesh_drain, 32);
    mesh_schedule = std::max(mesh_schedule, 32);
  }
  if (pending_dirty > 96 || pending_async > 32)
  {
    mesh_drain = std::max(mesh_drain, 48);
    mesh_schedule = std::max(mesh_schedule, 48);
  }
  else if (pending_dirty > 48 || pending_async > 16)
  {
    mesh_drain = std::max(mesh_drain, 32);
    mesh_schedule = std::max(mesh_schedule, 32);
  }
  else if (pending_dirty > 16 || pending_async > 8)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 20);
  }
  else if (pending_dirty > 0 || pending_async > 0)
  {
    mesh_drain = std::max(mesh_drain, 16);
    mesh_schedule = std::max(mesh_schedule, 16);
  }
  if (world.GetPlayerRelightMeshBurstFrames() > 0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }
  if (world.GetLastMovementFrameMs() > 24.0 && near_mesh_backlog)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }
  else if (world.GetLastMovementFrameMs() > 16.0 && !near_mesh_backlog)
  {
    mesh_drain = std::max(4, mesh_drain - 2);
    mesh_schedule = mesh_drain;
  }

  const MeshRebuildTickStats tick_stats = mesh_service.RebuildDirtyChunksWithStats(
      world.GetBlockWorld(), registry, mesh_drain, mesh_schedule);
  mesh_service.DrainAsyncMeshResults(world.GetBlockWorld(), registry, mesh_drain);
  world.FlushPendingRelightMeshColumns(8);

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
