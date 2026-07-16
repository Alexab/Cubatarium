#include "World/Streaming/WorldStreaming.h"
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
#include "WorldGen/Stages/WorldGenStages.h"
#include <chrono>

namespace cutum
{

namespace
{

constexpr float kBadFrameMs = 24.0f;
constexpr int kRelightBacklogStuckWindowMs = 1500;
constexpr int kRelightBgClampCooldownMs = 400;

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
    ChunkPopulator.reset();
    ChunkScheduler.reset();
    return;
  }
  ChunkPopulator = std::make_unique<UPipelineChunkPopulator>(
      *world.BlockRegistry, world.ObjectLibrary, world.WorldgenOwnerPackId);
  ChunkScheduler =
      std::make_unique<UChunkLoadScheduler>(*ChunkPopulator, ChunkGenTokens);
  ChunkScheduler->SetMarkDirtyFn(
      [this, &world](glm::ivec3 coord)
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
        if (SealFluidShoreOnChunkCommitted(
                world.BlockWorld, *world.BlockRegistry, settings,
                world.WorldgenOwnerPackId, coord))
        {
          const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE;
          world.MarkTerrainChunkMeshDirtySeamed(ground, 0, remesh_max_y, true);
        }
        if (!world.IsLightingRelightDeferred())
        {
          world.Persistence->EnqueueTerrainColumnRelight(ground.x * CHUNK_SIZE,
                                                         ground.z * CHUNK_SIZE,
                                                         near_focus);
        }
        // Mesh before async relight for object visibility; remesh after relight
        // via FlushPendingRelightMeshColumns.
        world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, 0, settings.MaxHeight, true);
      });
  ChunkScheduler->SetColumnMeshDirtyFn(
      [&world](glm::ivec3 groundCoord, int min_y, int max_y)
      {
        world.MarkTerrainChunkMeshDirtySeamed(groundCoord, min_y, max_y, true);
      });
  world.Persistence->EnsureChunkIoInitialized();
}

void UWorldStreaming::TickAsyncChunkSystems(UWorld &world)
{
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
  if (ChunkScheduler && procedural.AsyncChunkGeneration)
  {
    const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
    const glm::ivec3 focus_ground =
        UChunkManager::WorldToChunk(focus_block);
    const glm::ivec3 focus_horiz(focus_ground.x, 0, focus_ground.z);
    const int focus_radius = world.GetRenderDistanceChunks() + 1;
    const size_t mesh_dirty = world.GetMeshService().GetDirtyCount();
    const bool moving_fast =
        world.LastMovementSpeed > procedural.MovementSpeedBoostThreshold;
    if (moving_fast &&
        (mesh_dirty > 16 || pending_bg > 8 || frame_ms > 20.0))
    {
      chunk_budget.MaxChunkCommits = std::min(
          chunk_budget.MaxChunkCommits, procedural.MaxChunkCommitsPerFrame);
      chunk_budget.MaxLoadOps =
          std::min(chunk_budget.MaxLoadOps, procedural.MaxLoadOpsPerFrame);
    }
    if (mesh_dirty > 32 &&
        world.GetMeshService().HasDirtyWithinHorizontalRadius(focus_horiz,
                                                            focus_radius))
    {
      chunk_budget.MaxChunkCommits =
          std::max(1, chunk_budget.MaxChunkCommits / 2);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    if (frame_ms > kBadFrameMs)
    {
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits / 2);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    ChunkScheduler->Tick(world.BlockWorld, chunk_budget.MaxChunkCommits,
                         chunk_budget.MaxLoadOps);
  }
  int physics_budget = std::max(1, chunk_budget.MaxChunkCommits);
  if (frame_ms > kBadFrameMs)
  {
    physics_budget = 1;
  }
  const auto physics_t0 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < physics_budget && !DeferredPhysicsSeedQueue.empty(); ++i)
  {
    const glm::ivec3 coord = DeferredPhysicsSeedQueue.front();
    DeferredPhysicsSeedQueue.pop_front();
    ChunkPhysicsSeedBudgets seed_budgets;
    SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
  }
  world.PhysicsTelemetryData.CommitPhysicsMs +=
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - physics_t0)
          .count();
  world.Persistence->TickAsyncChunkIo(world);
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
  if (frame_ms > kBadFrameMs || flat_ms > 16.0)
  {
    bg_budget = std::max(0, bg_budget / 2);
  }
  if (now < gBgRelightClampUntil)
  {
    bg_budget = pending_bg > 0 ? std::min(bg_budget, 1) : 0;
  }

  world.Persistence->DrainRelightQueues(world, player_budget, bg_budget);
  world.PhysicsTelemetryData.PendingPlayerRelights =
      static_cast<uint64_t>(pending_player);
  world.PhysicsTelemetryData.PendingBackgroundRelights =
      static_cast<uint64_t>(pending_bg);
  world.PhysicsTelemetryData.AsyncRelightInflight =
      static_cast<uint64_t>(world.GetAsyncRelightInFlightCount());
  world.PhysicsTelemetryData.RelightDiscardedLate =
      world.GetRelightDiscardedLateCount();
  world.PhysicsTelemetryData.MeshDiscardedLate =
      world.GetMeshDiscardedLateCount();
}

void UWorldStreaming::QuiesceBackgroundWork(
    UWorld &world, const std::chrono::milliseconds async_io_timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  DeferredPhysicsSeedQueue.clear();
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

void UWorldStreaming::PauseChunkGeneration(
    const std::chrono::milliseconds worker_wait)
{
  if (!ChunkScheduler)
  {
    return;
  }
  ChunkScheduler->CancelAllPending(worker_wait);
  (void)ChunkScheduler->WaitForWorkersIdle(worker_wait);
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
      [&world](glm::ivec3 ground)
      {
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.FillWater && world.BlockRegistry)
        {
          const bool changed = SealFluidShoreOnChunkCommitted(
              world.BlockWorld, *world.BlockRegistry, settings,
              world.WorldgenOwnerPackId, ground);
          if (changed)
          {
            const int mesh_min_y = std::max(0, settings.SeaLevel - 8);
            const int mesh_max_y =
                std::min(settings.MaxHeight - 1, settings.SeaLevel + 8);
            world.MarkTerrainChunkMeshDirtySeamed(ground, mesh_min_y, mesh_max_y,
                                                  true);
          }
        }
        world.Persistence->EnqueueTerrainColumnRelight(ground.x * CHUNK_SIZE,
                                                       ground.z * CHUNK_SIZE);
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
    Streamer->SetRenderDistance(effectiveRenderDistance);
    meshService.SetRenderDistanceChunks(effectiveRenderDistance);

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
    Streamer->PrefetchAhead(
        UChunkManager::WorldToChunk(
            WorldPosToBlock(glm::vec3(eye.x, cap.feetY(eye) + 0.01f, eye.z))),
        forward, lastMovementSpeed, procedural.MovementSpeedBoostThreshold);
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
