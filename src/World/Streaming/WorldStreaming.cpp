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
        // Mesh must be scheduled on commit; deferring for async relight left
        // object/decoration voxels collidable but invisible until a later edit.
        world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, 0, settings.MaxHeight, true);
        DeferredPhysicsSeedQueue.push_back(coord);
        if (SealFluidShoreOnChunkCommitted(
                world.BlockWorld, *world.BlockRegistry, settings,
                world.WorldgenOwnerPackId, coord))
        {
          const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE;
          world.MarkTerrainChunkMeshDirtySeamed(ground, 0, remesh_max_y, true);
        }
        if (IsTerrainChunkComplete(world.BlockWorld, ground, settings.MaxHeight))
        {
          world.Persistence->EnqueueTerrainColumnRelight(ground.x * CHUNK_SIZE,
                                                         ground.z * CHUNK_SIZE);
        }
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
  if (ChunkScheduler && procedural.AsyncChunkGeneration)
  {
    const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
    const glm::ivec3 focus_ground =
        UChunkManager::WorldToChunk(focus_block);
    const glm::ivec3 focus_horiz(focus_ground.x, 0, focus_ground.z);
    const int focus_radius = world.GetRenderDistanceChunks() + 1;
    const size_t mesh_dirty = world.GetMeshService().GetDirtyCount();
    if (mesh_dirty > 32 &&
        world.GetMeshService().HasDirtyWithinHorizontalRadius(focus_horiz,
                                                            focus_radius))
    {
      chunk_budget.MaxChunkCommits =
          std::max(1, chunk_budget.MaxChunkCommits / 2);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    ChunkScheduler->Tick(world.BlockWorld, chunk_budget.MaxChunkCommits,
                         chunk_budget.MaxLoadOps);
  }
  const int physics_budget = std::max(1, chunk_budget.MaxChunkCommits);
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
  const int pending_bg = world.Persistence->GetPendingTerrainColumnRelightCount();
  const int player_budget = pending_player > 0 ? 2 : 0;
  const bool async_bg =
      procedural.AsyncRelight && !world.IsLightingRelightDeferred();
  const int bg_budget =
      async_bg ? (pending_bg > 24 ? 2 : 1)
               : (pending_bg > 12 ? 2 : (pending_bg > 0 ? 1 : 0));
  world.Persistence->DrainRelightQueues(world, player_budget, bg_budget);
  world.PhysicsTelemetryData.PendingPlayerRelights =
      static_cast<uint64_t>(pending_player);
  world.PhysicsTelemetryData.PendingBackgroundRelights =
      static_cast<uint64_t>(pending_bg);
}

void UWorldStreaming::QuiesceBackgroundWork(
    UWorld &world, const std::chrono::milliseconds async_io_timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  DeferredPhysicsSeedQueue.clear();
  if (ChunkScheduler)
  {
    ChunkScheduler->CancelAllPending(async_io_timeout);
  }
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
        world.MarkTerrainChunkMeshDirtySeamed(glm::ivec3(coord.x, 0, coord.z), 0,
                                              settings.MaxHeight, true);
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
        world.GetMeshService().RemoveChunk(coord);
        world.Collision.RemoveChunkMovementSolidCache(coord);
        if (coord.y == 0 && ChunkScheduler)
        {
          ChunkScheduler->Invalidate(coord);
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
