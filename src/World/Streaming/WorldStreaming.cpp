#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Physics/ChunkPhysicsSeed.h"
#include "App/Settings/RenderSettings.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Persistence/WorldPersistence.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include "World/Core/RuntimeTuning.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace cutum
{

namespace
{

constexpr float kBadFrameMs = 24.0f;
constexpr double kNearCompleteBudgetMs = 10.0;
constexpr double kFarStreamingBudgetMs = 5.0;
constexpr int kRelightBacklogStuckWindowMs = 1500;
constexpr int kRelightBgClampCooldownMs = 400;
constexpr int kAdaptiveRdMin = 3;
constexpr double kAdaptiveRdHysteresisSec = 2.5;

struct KeepPrewarmGate
{
  bool allow{false};
  int max_ops{0};
};

KeepPrewarmGate EvaluateKeepPrewarmGate(double frame_ms, int gen_backlog_total,
                                        int mesh_async, size_t mesh_dirty,
                                        bool near_mesh_backlog)
{
  KeepPrewarmGate gate;
  // Protect visual path only when near is actually starving AND gen is busy.
  // mesh_async saturation alone must not block keep prewarm (keep does not mesh).
  if (near_mesh_backlog && (gen_backlog_total > 6 || mesh_dirty > 96))
  {
    return gate;
  }
  if (gen_backlog_total > 16 || frame_ms > 32.0)
  {
    return gate;
  }
  // Soft prewarm: allow 1 op under moderate load so keep-shell actually warms.
  if (!near_mesh_backlog && gen_backlog_total < 12 && frame_ms < 24.0)
  {
    gate.allow = true;
    gate.max_ops = frame_ms < 14.0 && gen_backlog_total < 6 ? 2 : 1;
    return gate;
  }
  if (frame_ms < 18.0 && gen_backlog_total < 8 && mesh_dirty < 128)
  {
    gate.allow = true;
    gate.max_ops = 1;
    (void)mesh_async;
  }
  return gate;
}

int gLastBgRelightBacklog = 0;
std::chrono::steady_clock::time_point gLastBgRelightBacklogTs{};
std::chrono::steady_clock::time_point gBgRelightClampUntil{};

float QueryTerrainSurfaceWorldY(UWorld &world, const glm::vec3 &eye)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  const glm::ivec3 block = WorldPosToBlock(eye);
  const int surface_block_y = FindTopSolidSurfaceY(
      world.GetBlockWorld(), registry, block.x, block.z,
      world.GetProceduralSettings().MaxHeight);
  if (surface_block_y < 0)
  {
    return 0.0f;
  }
  return BlockTopY(surface_block_y);
}

} // namespace

UWorldStreaming::UWorldStreaming()
    : EmergeCoordinator(std::make_unique<UChunkEmergeCoordinator>())
{
}

UWorldStreaming::~UWorldStreaming() = default;

void UWorldStreaming::EnsureStreamer(UBlockWorld &blockWorld,
                                     UBlockRegistry &registry, uint32_t seed,
                                     const ProceduralSettings &settings)
{
  if (!Streamer)
  {
    Streamer = std::make_unique<UChunkStreamer>(blockWorld, registry, seed, 0,
                                                settings.MaxHeight);
    Streamer->SetRingGateEnabled(settings.RingGateEnabled);
  }
}

void UWorldStreaming::SetRenderDistance(int distance)
{
  if (Streamer)
  {
    Streamer->SetRenderDistance(distance);
  }
}

void UWorldStreaming::SetStreamerMaxLoadOpsPerFrame(int value)
{
  if (Streamer)
  {
    Streamer->SetMaxLoadOpsPerFrame(value);
  }
}

void UWorldStreaming::PrepareEnterGameSession(UWorld &world)
{
  if (!world.BlockRegistry)
  {
    return;
  }

  if (auto user = world.GetCurrentUser())
  {
    world.ApplyUserToCamera(user);
  }
  else
  {
    world.ApplySpawnToCamera();
  }
  world.ConsumeSpawnAreaPreparedByCooperativeLoad();
}

void UWorldStreaming::WarmupSpawnAreaForEnterGame(UWorld &world)
{
  PrepareEnterGameSession(world);
  world.WarmupVisibleListAtCamera();
}

void UWorldStreaming::InitChunkScheduler(UWorld &world)
{
  if (!world.BlockRegistry)
  {
    // Destroy scheduler first so workers cannot touch a freed populator.
    ChunkScheduler.reset();
    ChunkPopulator.reset();
    return;
  }
  // Cancel/join old pool before replacing the populator it references.
  if (ChunkScheduler)
  {
    ChunkScheduler->CancelAllPending(std::chrono::milliseconds(0));
    (void)ChunkScheduler->WaitForWorkersIdle(std::chrono::milliseconds(500));
    ChunkScheduler.reset();
  }
  ChunkPopulator = std::make_unique<UPipelineChunkPopulator>(
      *world.BlockRegistry, world.ObjectLibrary, world.WorldgenOwnerPackId);
  ChunkScheduler =
      std::make_unique<UChunkLoadScheduler>(*ChunkPopulator, ChunkGenTokens);
  ChunkScheduler->SetMarkDirtyFn(
      [this, &world](glm::ivec3 coord, int min_y, int max_y, bool fluid_sealed)
      {
        const glm::ivec3 ground(coord.x, 0, coord.z);
        if (Streamer)
        {
          Streamer->NotifyChunkCommitted(coord);
        }
        const ProceduralSettings &settings = world.GetProceduralSettings();
        const glm::ivec3 focus_ground =
            UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
        const int focus_radius = world.GetRenderDistanceChunks() + 1;
        const bool near_focus =
            std::abs(coord.x - focus_ground.x) <= focus_radius &&
            std::abs(coord.z - focus_ground.z) <= focus_radius;
        DeferredPhysicsSeedQueue.push_back(coord);
        if (settings.FillWater)
        {
          // V_fluid: IntraChunkSeal once (populate XOR commit); LiveShoreAir always.
          if (!fluid_sealed)
          {
            const auto seal_t0 = std::chrono::high_resolution_clock::now();
            SealFluidShoreOnChunkCommitted(
                world.BlockWorld, *world.BlockRegistry, settings,
                world.WorldgenOwnerPackId, coord, /*include_shore_air=*/false);
            world.PhysicsTelemetryData.CommitSealMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - seal_t0)
                    .count();
          }
          DeferredShoreSealQueue.push_back(coord);
        }
        if (!world.IsLightingRelightDeferred())
        {
          // Full column height for skylight: narrow C2 Y-band is for mesh dirty
          // only. Relight must see sky→surface or meshes stay black after emerge.
          world.Persistence->EnqueueTerrainColumnRelight(
              ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, near_focus,
              /*min_y=*/0, settings.MaxHeight);
        }
        // Single seamed dirty for occupied Y / sea band; full height remesh
        // after async relight via FlushPendingRelightMeshColumns.
        int dirty_min = std::max(0, min_y);
        int dirty_max = std::min(settings.MaxHeight, max_y);
        if (settings.FillWater)
        {
          dirty_min =
              std::min(dirty_min, std::max(0, settings.SeaLevel - CHUNK_SIZE));
          dirty_max = std::max(
              dirty_max,
              std::min(settings.MaxHeight, settings.SeaLevel + CHUNK_SIZE));
        }
        if (dirty_max < dirty_min)
        {
          dirty_min = 0;
          dirty_max = settings.MaxHeight;
        }
        if (!world.IsLightingRelightDeferred())
        {
          // Light before first mesh for all new columns (near and far). Far used
          // to mark dirty immediately → permanent black under remesh backlog.
          world.NotePendingLightBeforeMesh(ground, dirty_min, dirty_max);
        }
        else if (near_focus)
        {
          world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, dirty_min, dirty_max, true);
        }
        else
        {
          world.MeshService->MarkTerrainChunkMeshDirtySeamed(
              ground, dirty_min, dirty_max, false);
        }
      });
  world.Persistence->EnsureChunkIoInitialized();
}

void UWorldStreaming::TickAsyncChunkSystems(UWorld &world)
{
  const auto main_t0 = std::chrono::high_resolution_clock::now();
  auto elapsed_main_ms = [&]()
  {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - main_t0)
        .count();
  };

  world.PhysicsTelemetryData.CommitApplyMs = 0.0;
  world.PhysicsTelemetryData.CommitSealMs = 0.0;
  world.PhysicsTelemetryData.CommitPhysicsMs = 0.0;
  world.PhysicsTelemetryData.IdlePrefetchMs = 0.0;

  const ProceduralSettings &procedural = world.GetProceduralSettings();
  EmergeCoordinator->BeginFrame(procedural, world.LastMovementSpeed,
                                world.MaxLoadOpsPerFrame,
                                world.GetLastMovementFrameMs());
  const UChunkEmergeCoordinator::FrameBudget budget =
      EmergeCoordinator->GetLastBudget();
  UChunkEmergeCoordinator::FrameBudget chunk_budget = budget;
  const double frame_ms = world.GetLastMovementFrameMs();
  const int pending_bg =
      world.Persistence ? world.Persistence->GetPendingTerrainColumnRelightCount()
                        : 0;

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground = UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_horiz(focus_ground.x, 0, focus_ground.z);
  const int visual_rd =
      Streamer ? Streamer->GetVisualRenderDistance()
               : world.GetRenderDistanceChunks();
  const int focus_radius = visual_rd + 1;
  const size_t mesh_dirty = world.GetMeshService().GetDirtyCount();
  const bool near_mesh_backlog =
      world.GetMeshService().HasDirtyWithinHorizontalRadius(focus_horiz,
                                                           focus_radius) ||
      world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, focus_radius);
  const int gen_backlog_total =
      ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
  const int mesh_async = world.GetMeshService().GetAsyncInFlightCount();
  const bool keep_prewarm_surplus =
      EvaluateKeepPrewarmGate(frame_ms, gen_backlog_total, mesh_async, mesh_dirty,
                              near_mesh_backlog)
          .allow;

  auto is_near_column = [&](glm::ivec3 coord)
  {
    return std::max(std::abs(coord.x - focus_horiz.x),
                    std::abs(coord.z - focus_horiz.z)) <= focus_radius;
  };
  auto near_exhausted = [&]()
  { return elapsed_main_ms() >= kNearCompleteBudgetMs; };
  auto far_exhausted = [&]()
  {
    if (!keep_prewarm_surplus)
    {
      return true;
    }
    return elapsed_main_ms() >=
           (kNearCompleteBudgetMs + kFarStreamingBudgetMs);
  };

  if (ChunkScheduler && procedural.AsyncChunkGeneration)
  {
    const bool moving_fast =
        world.LastMovementSpeed > procedural.MovementSpeedBoostThreshold;
    if (moving_fast &&
        (mesh_dirty > 16 || pending_bg > 8 || frame_ms > 20.0) &&
        !near_mesh_backlog)
    {
      chunk_budget.MaxChunkCommits = std::min(
          chunk_budget.MaxChunkCommits, procedural.MaxChunkCommitsPerFrame);
      chunk_budget.MaxLoadOps =
          std::min(chunk_budget.MaxLoadOps, procedural.MaxLoadOpsPerFrame);
    }
    // Near-complete first: do not starve commits when focus Dirty is high.
    const bool missing_near_mesh =
        world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(), focus_horiz, focus_radius);
    bool incomplete_near_column = false;
    {
      const int max_y = procedural.MaxHeight;
      const int scan_r = std::min(focus_radius, visual_rd);
      for (int dz = -scan_r; dz <= scan_r && !incomplete_near_column; ++dz)
      {
        for (int dx = -scan_r; dx <= scan_r; ++dx)
        {
          const glm::ivec3 ground(focus_horiz.x + dx, 0, focus_horiz.z + dz);
          if (!IsTerrainChunkComplete(world.BlockWorld, ground, max_y))
          {
            incomplete_near_column = true;
            break;
          }
        }
      }
    }
    const bool near_focus_holes =
        missing_near_mesh || incomplete_near_column ||
        world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius);
    if (near_mesh_backlog || near_focus_holes)
    {
      chunk_budget.MaxChunkCommits =
          std::max(chunk_budget.MaxChunkCommits, 2);
      chunk_budget.MaxLoadOps = std::max(chunk_budget.MaxLoadOps, 2);
    }
    else if (mesh_dirty > 32)
    {
      chunk_budget.MaxChunkCommits =
          std::max(1, chunk_budget.MaxChunkCommits / 2);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    // Standing with large dirty: stop feeding NEW far loads, but never starve
    // the focus ring while columns/meshes under the camera are incomplete.
    if (!moving_fast && mesh_dirty > 96 && !near_focus_holes)
    {
      chunk_budget.MaxLoadOps = 0;
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    else if (!moving_fast && mesh_dirty > 48)
    {
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    if (!moving_fast && near_focus_holes)
    {
      chunk_budget.MaxLoadOps = std::max(chunk_budget.MaxLoadOps, 2);
      chunk_budget.MaxChunkCommits =
          std::max(chunk_budget.MaxChunkCommits, 2);
    }
    if (frame_ms > kBadFrameMs && !near_mesh_backlog)
    {
      chunk_budget.MaxChunkCommits = std::min(chunk_budget.MaxChunkCommits, 1);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    const int completed_ready =
        ChunkScheduler ? ChunkScheduler->GetCompletedReadyCount() : 0;
    if (gen_backlog_total > 0 || completed_ready > 0)
    {
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    ChunkScheduler->Tick(world.BlockWorld, chunk_budget.MaxChunkCommits,
                         chunk_budget.MaxLoadOps);
    world.PhysicsTelemetryData.CommitApplyMs =
        ChunkScheduler->GetLastTickApplyMs();
  }

  auto finish_telemetry = [&]()
  {
    world.PhysicsTelemetryData.PendingPlayerRelights = static_cast<uint64_t>(
        world.Persistence ? world.Persistence->GetPendingPlayerRelightCount()
                          : 0);
    world.PhysicsTelemetryData.PendingBackgroundRelights =
        static_cast<uint64_t>(pending_bg);
    world.PhysicsTelemetryData.AsyncRelightInflight =
        static_cast<uint64_t>(world.GetAsyncRelightInFlightCount());
    world.PhysicsTelemetryData.RelightDiscardedLate =
        world.GetRelightDiscardedLateCount();
    world.PhysicsTelemetryData.MeshDiscardedLate =
        world.GetMeshDiscardedLateCount();
    if (Streamer)
    {
      const int v = Streamer->GetVisualRenderDistance();
      const int k = Streamer->GetKeepRenderDistance();
      world.PhysicsTelemetryData.VisualCols = (2 * v + 1) * (2 * v + 1);
      world.PhysicsTelemetryData.KeepCols = (2 * k + 1) * (2 * k + 1);
    }
    world.PhysicsTelemetryData.GenBacklogTotal = gen_backlog_total;
  };

  // Physics seed: near first (two-pass), then far only when idle.
  {
    int near_done = 0;
    int far_done = 0;
    const int near_physics_budget = near_mesh_backlog ? 4 : 2;
    const int far_physics_budget = keep_prewarm_surplus ? 1 : 0;
    const auto physics_t0 = std::chrono::high_resolution_clock::now();
    for (auto it = DeferredPhysicsSeedQueue.begin();
         it != DeferredPhysicsSeedQueue.end() &&
         near_done < near_physics_budget && !near_exhausted();)
    {
      if (!is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredPhysicsSeedQueue.erase(it);
      ChunkPhysicsSeedBudgets seed_budgets;
      SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
      ++near_done;
    }
    for (auto it = DeferredPhysicsSeedQueue.begin();
         it != DeferredPhysicsSeedQueue.end() &&
         far_done < far_physics_budget && !far_exhausted();)
    {
      if (is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredPhysicsSeedQueue.erase(it);
      ChunkPhysicsSeedBudgets seed_budgets;
      SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
      ++far_done;
    }
    world.PhysicsTelemetryData.CommitPhysicsMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - physics_t0)
            .count();
  }

  // Shore seal: aggressive near drain, far only when idle.
  {
    const int near_shore_budget = near_mesh_backlog ? 4 : 3;
    const int far_shore_budget = keep_prewarm_surplus ? 1 : 0;
    int near_done = 0;
    int far_done = 0;
    auto seal_one = [&](glm::ivec3 coord, bool near_column)
    {
      const ProceduralSettings &settings = world.GetProceduralSettings();
      const auto seal_t0 = std::chrono::high_resolution_clock::now();
      const bool changed = SealFluidShoreAirOnChunkCommitted(
          world.BlockWorld, *world.BlockRegistry, settings,
          world.WorldgenOwnerPackId, coord);
      world.PhysicsTelemetryData.CommitSealMs +=
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - seal_t0)
              .count();
      if (changed)
      {
        const glm::ivec3 ground(coord.x, 0, coord.z);
        const int mesh_min_y = std::max(0, settings.SeaLevel - 8);
        const int mesh_max_y =
            std::min(settings.MaxHeight - 1, settings.SeaLevel + 8);
        if (near_column)
        {
          world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, mesh_min_y, mesh_max_y, true);
        }
        else
        {
          world.MeshService->MarkTerrainChunkMeshDirtySeamed(
              ground, mesh_min_y, mesh_max_y, false);
        }
      }
    };
    for (auto it = DeferredShoreSealQueue.begin();
         it != DeferredShoreSealQueue.end() &&
         near_done < near_shore_budget && !near_exhausted();)
    {
      if (!is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredShoreSealQueue.erase(it);
      seal_one(coord, true);
      ++near_done;
    }
    for (auto it = DeferredShoreSealQueue.begin();
         it != DeferredShoreSealQueue.end() &&
         far_done < far_shore_budget && !far_exhausted();)
    {
      if (is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredShoreSealQueue.erase(it);
      seal_one(coord, false);
      ++far_done;
    }
  }

  if (!near_exhausted())
  {
    world.Persistence->TickAsyncChunkIo(world);
  }
  const int pending_player = world.Persistence->GetPendingPlayerRelightCount();
  const int player_budget = pending_player > 0 ? 2 : 0;
  const bool async_bg =
      procedural.AsyncRelight && !world.IsLightingRelightDeferred();
  int bg_budget =
      async_bg ? (pending_bg > 24 ? 3 : (pending_bg > 8 ? 2 : 1))
               : (pending_bg > 12 ? 2 : (pending_bg > 0 ? 1 : 0));

  const double flat_ms = world.GetMeshService().GetLastFlatRebuildMs();
  const auto now = std::chrono::steady_clock::now();
  if (gLastBgRelightBacklogTs == std::chrono::steady_clock::time_point{})
  {
    gLastBgRelightBacklogTs = now;
    gLastBgRelightBacklog = pending_bg;
  }
  else if (now - gLastBgRelightBacklogTs >=
           std::chrono::milliseconds(kRelightBacklogStuckWindowMs))
  {
    if (pending_bg >= gLastBgRelightBacklog)
    {
      gBgRelightClampUntil =
          now + std::chrono::milliseconds(kRelightBgClampCooldownMs);
    }
    gLastBgRelightBacklog = pending_bg;
    gLastBgRelightBacklogTs = now;
  }
  if ((frame_ms > kBadFrameMs || flat_ms > 16.0) && !near_mesh_backlog)
  {
    bg_budget = std::max(0, bg_budget / 2);
  }
  if (now < gBgRelightClampUntil)
  {
    bg_budget = pending_bg > 0 ? std::min(std::max(bg_budget, 1), 1) : 0;
  }
  // Far-budget gate may throttle prewarm, but must not cancel lighting for
  // already-committed columns: meshes built at light=0 stay black forever.
  if (far_exhausted() && pending_player == 0 && !near_mesh_backlog &&
      pending_bg == 0)
  {
    bg_budget = 0;
  }
  if (pending_bg > 0)
  {
    bg_budget = std::max(bg_budget, 1);
    // Idle/FPS-recovered frames: catch up lighting instead of idling black.
    if (!near_mesh_backlog && frame_ms <= kBadFrameMs)
    {
      bg_budget = std::max(bg_budget, pending_bg > 16 ? 4 : 2);
    }
  }
  // Near holes / awaiting first light: keep draining even under hitch frames.
  if (pending_bg > 0 &&
      (near_mesh_backlog ||
       world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius)))
  {
    bg_budget = std::max(bg_budget, pending_bg > 8 ? 4 : 2);
  }
  // Columns enqueued far stay in the non-priority FIFO forever unless promoted
  // when the player approaches — that left transparent PendingLightBeforeMesh holes.
  if (pending_bg > 0)
  {
    world.Persistence->PromoteNearTerrainColumnRelights(focus_horiz,
                                                        focus_radius);
  }
  world.PromotePendingLightRelightsNear(focus_horiz, focus_radius);

  world.Persistence->DrainRelightQueues(world, player_budget, bg_budget);
  finish_telemetry();
}

void UWorldStreaming::QuiesceBackgroundWork(
    UWorld &world, const std::chrono::milliseconds async_io_timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  DeferredPhysicsSeedQueue.clear();
  DeferredShoreSealQueue.clear();
  PauseChunkGeneration(async_io_timeout);
  if (world.Persistence)
  {
    world.Persistence->ClearPendingRelights();
    for (int pass = 0; pass < 64; ++pass)
    {
      if (world.Persistence->TickDrainAsyncChunkIo(world, 8))
      {
        break;
      }
    }
    (void)world.Persistence->AbortAsyncChunkIoFor(async_io_timeout);
  }
}

void UWorldStreaming::CancelChunkGeneration()
{
  if (!ChunkScheduler)
  {
    return;
  }
  // Zero wait: clear queue + bump tokens so shouldCancel trips; do not join.
  ChunkScheduler->CancelAllPending(std::chrono::milliseconds(0));
}

void UWorldStreaming::AbandonWorkersForProcessExit(
    const std::chrono::milliseconds timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  SyncCoarseCacheGround = glm::ivec3(INT32_MAX, 0, INT32_MAX);
  CancelChunkGeneration();
  if (!ChunkScheduler)
  {
    ChunkPopulator.reset();
    return;
  }
  if (ChunkScheduler->WaitForWorkersIdle(timeout))
  {
    ChunkScheduler.reset();
    ChunkPopulator.reset();
    return;
  }
  // Workers still in carve/populate: detach threads, then leak scheduler +
  // populator so in-flight jobs do not use-after-free during process teardown.
  ChunkScheduler->ShutdownForProcessExit(std::chrono::milliseconds(0));
  (void)ChunkScheduler.release();
  (void)ChunkPopulator.release();
}

void UWorldStreaming::PauseChunkGeneration(
    const std::chrono::milliseconds worker_wait)
{
  if (!ChunkScheduler)
  {
    return;
  }
  ChunkScheduler->CancelAllPending(worker_wait);
  if (worker_wait.count() > 0)
  {
    (void)ChunkScheduler->WaitForWorkersIdle(worker_wait);
  }
}

void UWorldStreaming::ResumeStreamerAfterQuiesce()
{
  if (Streamer && StreamingEnabled)
  {
    Streamer->SetEnabled(true);
  }
}

void UWorldStreaming::TickMeshEmerge(UWorld &world)
{
  EmergeCoordinator->TickMeshEmerge(world);
  world.PhysicsTelemetryData.MeshSyncMs =
      world.GetMeshService().GetLastMeshSyncMs();
  world.PhysicsTelemetryData.MeshSnapshotMs =
      world.GetMeshService().GetLastMeshSnapshotMs();
}

void UWorldStreaming::InitStreamerCallbacks(UWorld &world)
{
  if (!Streamer || !world.BlockRegistry)
  {
    return;
  }
  InitChunkScheduler(world);
  const ProceduralSettings &procedural = world.GetProceduralSettings();

  Streamer->SetRenderDistance(world.RenderDistanceChunks);
  Streamer->SetMaxLoadOpsPerFrame(world.MaxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(world.MaxUnloadOpsPerFrame);
  Streamer->SetMaxTerrainHeight(procedural.MaxHeight);
  Streamer->SetEnabled(StreamingEnabled);
  Streamer->SetWorldFolder(world.GetWorldFolderPath());
  Streamer->SetCallbacks(
      [this, &world](glm::ivec3 coord)
      {
        UWorldPersistence &persistence = *world.Persistence;
        if (persistence.GetChunkStorage().IsColumnSavePending(coord))
        {
          return false;
        }
        if (persistence.IsTerrainColumnDiskLoadPending(coord))
        {
          return false;
        }
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.AsyncChunkIo)
        {
          persistence.RequestAsyncTerrainColumnLoad(world, coord);
          return false;
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        const bool loaded =
            persistence.LoadTerrainColumn(coord, world.BlockWorld,
                                          *world.BlockRegistry,
                                          settings.MaxHeight) > 0;
        if (loaded)
        {
          if (!IsTerrainChunkComplete(world.BlockWorld, coord,
                                      settings.MaxHeight))
          {
            persistence.PurgeIncompleteTerrainColumn(world.BlockWorld, coord,
                                                     settings.MaxHeight);
            FrameStreamingIoMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - t0)
                    .count();
            return false;
          }
          persistence.EnqueueTerrainColumnRelight(coord.x * CHUNK_SIZE,
                                                  coord.z * CHUNK_SIZE);
        }
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
        return loaded;
      },
      [this, &world](glm::ivec3 coord)
      {
        UWorldPersistence &persistence = *world.Persistence;
        const glm::ivec3 ground(coord.x, 0, coord.z);
        persistence.CancelAsyncTerrainColumnLoad(ground);
        ChunkGenTokens.Bump(ground);
        if (ChunkScheduler)
        {
          ChunkScheduler->Invalidate(ground);
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.AsyncChunkIo)
        {
          persistence.RequestAsyncTerrainColumnSave(world, ground);
        }
        else
        {
          persistence.SaveTerrainColumn(ground, world.BlockWorld,
                                        *world.BlockRegistry,
                                        settings.MaxHeight);
          ChunkGenTokens.Bump(ground);
          if (ChunkScheduler)
          {
            ChunkScheduler->Invalidate(ground);
          }
        }
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
      },
      [&world](glm::ivec3 coord)
      {
        const ProceduralSettings &settings = world.GetProceduralSettings();
        const int remesh_min_y = std::max(0, settings.SeaLevel - CHUNK_SIZE);
        const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
        world.MarkTerrainChunkMeshDirtySeamed(glm::ivec3(coord.x, 0, coord.z),
                                              remesh_min_y, remesh_max_y, true);
      },
      [this, &world](int x, int z)
      {
        if (!world.AllowProceduralFill || !world.WorldGen)
        {
          return;
        }
        // Same CoarseHeightCache as async Populate (D: unify sampling entry).
        if (auto *composable =
                dynamic_cast<UComposableWorldGenerator *>(world.WorldGen.get()))
        {
          const glm::ivec3 ground(FloorDiv(x, CHUNK_SIZE), 0,
                                  FloorDiv(z, CHUNK_SIZE));
          if (ground != SyncCoarseCacheGround)
          {
            if (SyncCoarseCacheGround.x != INT32_MAX)
            {
              composable->EndChunkCoarseCache();
            }
            const int blend_pad = std::clamp(
                static_cast<int>(
                    std::lround(world.GetProceduralSettings().Tuning.biomeBlendRadius)),
                0, 16);
            composable->BeginChunkCoarseCache(ground.x * CHUNK_SIZE,
                                              ground.z * CHUNK_SIZE,
                                              blend_pad + 8);
            SyncCoarseCacheGround = ground;
          }
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        world.WorldGen->GenerateColumn(x, z);
        FrameStreamingGenMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
      },
      [this, &world](glm::ivec3 coord)
      {
        world.Collision.RemoveChunkMovementSolidCache(coord);
        if (coord.y == 0 && ChunkScheduler)
        {
          ChunkScheduler->Invalidate(coord);
        }
      });
  Streamer->SetUnloadColumnCallback(
      [this, &world](glm::ivec3 ground, int max_cy)
      {
        world.ClearPendingLightBeforeMesh(glm::ivec2(ground.x, ground.z));
        world.GetMeshService().RemoveColumn(ground, max_cy);
        for (int cy = 0; cy <= max_cy; ++cy)
        {
          world.Collision.RemoveChunkMovementSolidCache(
              glm::ivec3(ground.x, cy, ground.z));
        }
        if (ChunkScheduler)
        {
          ChunkScheduler->Invalidate(ground);
        }
      });
  Streamer->SetAsyncGeneration(procedural.AsyncChunkGeneration);
  Streamer->SetAsyncCallbacks(
      [this, &world, &procedural](glm::ivec3 coord, int priority)
      {
        if (ChunkScheduler)
        {
          glm::ivec2 column_origin(0);
          bool has_origin = false;
          if (auto camera = world.GetCurrentUserCamera())
          {
            const PlayerCapsule cap = camera->GetPlayerCapsule();
            const glm::vec3 feet(camera->GetPosition().x,
                                 cap.feetY(camera->GetPosition()) + 0.01f,
                                 camera->GetPosition().z);
            const glm::ivec3 feet_block = WorldPosToBlock(feet);
            column_origin = glm::ivec2(feet_block.x, feet_block.z);
            has_origin = true;
          }
          ChunkScheduler->RequestLoad(coord, priority, procedural, column_origin,
                                      has_origin);
        }
      },
      [&world, this](glm::ivec3 coord)
      {
        if (ChunkScheduler && ChunkScheduler->IsPending(coord))
        {
          return false;
        }
        if (!world.BlockWorld.GetChunkManager().HasChunk(coord))
        {
          return false;
        }
        return IsTerrainChunkComplete(world.BlockWorld, coord,
                                      world.GetProceduralSettings().MaxHeight);
      });
  Streamer->SetColumnPendingCallback(
      [&world](glm::ivec3 coord)
      { return world.Persistence->IsTerrainColumnDiskLoadPending(coord); });
  Streamer->SetGenerationLightingHooks(
      [&world](bool deferred) { world.SetLightingRelightDeferred(deferred); },
      [this, &world](glm::ivec3 ground)
      {
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.FillWater && world.BlockRegistry)
        {
          SealFluidShoreOnChunkCommitted(
              world.BlockWorld, *world.BlockRegistry, settings,
              world.WorldgenOwnerPackId, ground, /*include_shore_air=*/false);
          DeferredShoreSealQueue.push_back(ground);
        }
        const glm::ivec3 focus_ground =
            UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
        const int focus_radius = world.GetRenderDistanceChunks() + 1;
        const bool near_focus =
            std::max(std::abs(ground.x - focus_ground.x),
                     std::abs(ground.z - focus_ground.z)) <= focus_radius;
        world.Persistence->EnqueueTerrainColumnRelight(
            ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, near_focus);
      });
}

void UWorldStreaming::RefreshStreamerSettings(const ProceduralSettings &settings,
                                              int maxLoadOpsPerFrame,
                                              int maxUnloadOpsPerFrame)
{
  if (!Streamer)
  {
    return;
  }
  Streamer->SetAsyncGeneration(settings.AsyncChunkGeneration);
  Streamer->SetMaxTerrainHeight(settings.MaxHeight);
  Streamer->SetMaxLoadOpsPerFrame(maxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(maxUnloadOpsPerFrame);
  Streamer->SetRingGateEnabled(settings.RingGateEnabled);
}

void UWorldStreaming::UpdateStreaming(UWorld &world,
                                      UWorldMeshService &meshService,
                                      const RenderSettings &render,
                                      int renderDistanceChunks,
                                      int &effectiveRenderDistance,
                                      float &effectiveFogStartRatio,
                                      StreamingAltitudePolicyParams &altitudeParams,
                                      glm::vec3 &lastCameraPosition,
                                      float &lastMovementSpeed)
{
  if (!Streamer || !StreamingEnabled)
  {
    return;
  }
  if (auto camera = world.GetCurrentUserCamera())
  {
    const PlayerCapsule cap = camera->GetPlayerCapsule();
    const glm::vec3 eye = camera->GetPosition();
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      Streamer->SetViewForward(forward);
    }
    if (render.AltitudeAdaptiveFog)
    {
      altitudeParams.AltitudeThresholdBlocks = render.AltitudeFogThresholdBlocks;
      altitudeParams.RenderDistancePenaltyPerChunk = 1;
      altitudeParams.FogStartRatioBoost =
          std::max(0.15f, render.AltitudeFogPenaltyPer16Blocks * 4.0f);
      const float ground_y = render.AltitudeUseTerrainSurface
                                 ? QueryTerrainSurfaceWorldY(world, eye)
                                 : cap.feetY(eye);
      world.SetAltitudeAboveTerrain(std::max(0.0f, eye.y - ground_y));
      meshService.SetAltitudeCullState(world.GetAltitudeAboveTerrain(),
                                       render.AltitudeFogThresholdBlocks);
      const StreamingAltitudeSnapshot alt = ComputeStreamingAltitude(
          renderDistanceChunks, eye.y, ground_y,
          render.DistanceFogStartRatio, altitudeParams);
      effectiveRenderDistance = alt.EffectiveRenderDistance;
      effectiveFogStartRatio = alt.EffectiveFogStartRatio;
    }
    else
    {
      world.SetAltitudeAboveTerrain(0.0f);
      meshService.SetAltitudeCullState(0.0f, render.AltitudeFogThresholdBlocks);
      effectiveRenderDistance = renderDistanceChunks;
      effectiveFogStartRatio = render.DistanceFogStartRatio;
    }

    // Optional adaptive RD under streaming pressure (hysteresis).
    PhysMsEma = PhysMsEma <= 0.0
                    ? world.GetPhysicsTelemetry().PhysicsStepMs
                    : (0.85 * PhysMsEma +
                       0.15 * world.GetPhysicsTelemetry().PhysicsStepMs);
    if (render.AdaptiveRenderDistance)
    {
      if (AdaptiveEffectiveRd < 0)
      {
        AdaptiveEffectiveRd = effectiveRenderDistance;
      }
      const size_t dirty = meshService.GetDirtyCount();
      const int gen_backlog_total =
          ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
      const auto now = std::chrono::steady_clock::now();
      const double since_adjust =
          AdaptiveRdLastAdjust.time_since_epoch().count() == 0
              ? kAdaptiveRdHysteresisSec
              : std::chrono::duration<double>(now - AdaptiveRdLastAdjust)
                    .count();
      if (since_adjust >= kAdaptiveRdHysteresisSec)
      {
        int next = AdaptiveEffectiveRd;
        if (dirty > 64 || gen_backlog_total > 12 || PhysMsEma > 40.0)
        {
          next = std::max(kAdaptiveRdMin, AdaptiveEffectiveRd - 1);
        }
        else if (dirty < 24 && PhysMsEma < 20.0)
        {
          next = std::min(effectiveRenderDistance, AdaptiveEffectiveRd + 1);
        }
        if (next != AdaptiveEffectiveRd)
        {
          AdaptiveEffectiveRd = next;
          AdaptiveRdLastAdjust = now;
        }
      }
      AdaptiveEffectiveRd =
          std::clamp(AdaptiveEffectiveRd, kAdaptiveRdMin, effectiveRenderDistance);
      effectiveRenderDistance = AdaptiveEffectiveRd;
    }
    else
    {
      AdaptiveEffectiveRd = -1;
    }

    Streamer->SetKeepPrefetchMargin(
        URuntimeTuning::Get().KeepPrefetchMargin);
    Streamer->SetMaxKeepPrefetchOpsPerFrame(
        URuntimeTuning::Get().MaxKeepPrefetchOpsPerFrame);
    Streamer->SetRenderDistance(effectiveRenderDistance);
    // Visual cull/mesh focus = visual RD; keep ring is Visual+margin.
    meshService.SetRenderDistanceChunks(
        Streamer->GetVisualRenderDistance());

    const float dt = std::max(0.0001f, camera->GetDeltaTime());
    const glm::vec3 delta = eye - lastCameraPosition;
    lastMovementSpeed = glm::length(glm::vec3(delta.x, 0.0f, delta.z)) / dt;
    lastCameraPosition = eye;

    const ProceduralSettings &procedural = world.GetProceduralSettings();
    const double frame_ms = world.GetLastMovementFrameMs();
    int unload_ops = world.MaxUnloadOpsPerFrame;
    if (frame_ms > 24.0)
    {
      unload_ops = 0;
    }
    else if (frame_ms > 16.0)
    {
      unload_ops = std::min(unload_ops, 1);
    }
    Streamer->SetEffectiveUnloadOpsPerFrame(unload_ops);
    Streamer->Update(WorldPosToBlock(eye), eye, cap);

    const glm::ivec3 feet_chunk = UChunkManager::WorldToChunk(
        WorldPosToBlock(glm::vec3(eye.x, cap.feetY(eye) + 0.01f, eye.z)));
    const glm::ivec3 focus_horiz(feet_chunk.x, 0, feet_chunk.z);
    const int focus_radius = Streamer->GetVisualRenderDistance() + 1;
    const size_t dirty = meshService.GetDirtyCount();
    const int gen_backlog_total =
        ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
    const int mesh_async = meshService.GetAsyncInFlightCount();
    const bool near_mesh_backlog =
        meshService.HasDirtyWithinHorizontalRadius(focus_horiz, focus_radius) ||
        meshService.HasMissingGreedyMeshInHorizontalRadius(world.GetBlockWorld(),
                                                         focus_horiz,
                                                         focus_radius);
    const KeepPrewarmGate keep_gate = EvaluateKeepPrewarmGate(
        frame_ms, gen_backlog_total, mesh_async, dirty, near_mesh_backlog);
    const auto prefetch_t0 = std::chrono::high_resolution_clock::now();
    int prefetch_visual_ops = 0;
    Streamer->PrefetchAhead(feet_chunk, forward, lastMovementSpeed,
                            procedural.MovementSpeedBoostThreshold,
                            &prefetch_visual_ops);
    int prefetch_keep_ops = 0;
    if (keep_gate.allow)
    {
      const int keep_budget = std::min(
          keep_gate.max_ops, URuntimeTuning::Get().MaxKeepPrefetchOpsPerFrame);
      Streamer->PrefetchKeepShell(feet_chunk, keep_budget, &prefetch_keep_ops);
    }
    world.PhysicsTelemetryData.IdlePrefetchMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - prefetch_t0)
            .count();
    world.PhysicsTelemetryData.PrefetchVisualOps = prefetch_visual_ops;
    world.PhysicsTelemetryData.PrefetchKeepOps = prefetch_keep_ops;
    world.PhysicsTelemetryData.GenBacklogTotal = gen_backlog_total;
    world.PhysicsTelemetryData.VisualCols =
        (2 * Streamer->GetVisualRenderDistance() + 1) *
        (2 * Streamer->GetVisualRenderDistance() + 1);
    world.PhysicsTelemetryData.KeepCols =
        (2 * Streamer->GetKeepRenderDistance() + 1) *
        (2 * Streamer->GetKeepRenderDistance() + 1);
  }
}

void UWorldStreaming::EnsureCollisionChunks(const glm::ivec3 &feetBlock,
                                            const glm::vec3 &forward)
{
  if (!Streamer || !StreamingEnabled)
  {
    return;
  }
  if (glm::length(forward) > 0.01f)
  {
    Streamer->SetViewForward(forward);
  }
  Streamer->EnsureCollisionChunks(feetBlock);
}

void UWorldStreaming::ResetFrameTiming()
{
  FrameStreamingGenMs = 0.0;
  FrameStreamingIoMs = 0.0;
}

const StreamingFrameStats *UWorldStreaming::GetLastFrameStats() const
{
  return Streamer ? &Streamer->GetLastFrameStats() : nullptr;
}

void UWorldStreaming::MarkPersistedColumnsFromWorld()
{
  if (Streamer)
  {
    Streamer->MarkPersistedColumnsFromWorld();
  }
}

} // namespace cutum
