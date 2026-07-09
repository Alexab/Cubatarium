#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Physics/ChunkPhysicsSeed.h"
#include "App/Settings/RenderSettings.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Persistence/WorldPersistence.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <chrono>

namespace cutum
{

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

void UWorldStreaming::WarmupSpawnAreaForEnterGame(UWorld &world)
{
  if (!world.BlockRegistry)
  {
    return;
  }

  world.InitStreamerCallbacks();
  if (auto user = world.GetCurrentUser())
  {
    world.ApplyUserToCamera(user);
  }
  else
  {
    world.ApplySpawnToCamera();
  }

  const int prev_load_ops = world.MaxLoadOpsPerFrame;
  const int warmup_load_ops =
      std::max(prev_load_ops,
               (2 * world.RenderDistanceChunks + 1) *
                   (2 * world.RenderDistanceChunks + 1));
  world.MaxLoadOpsPerFrame = warmup_load_ops;
  SetStreamerMaxLoadOpsPerFrame(warmup_load_ops);

  constexpr int kMeshFlushBudget = UChunkEmergeCoordinator::kWarmupMeshFlush;
  const UChunkEmergeCoordinator::FrameBudget warmup_budget =
      UChunkEmergeCoordinator::WarmupBudget(kMeshFlushBudget);
  for (int pass = 0; pass < 48; ++pass)
  {
    world.UpdateStreaming();
    world.TickAsyncChunkSystems();
    world.MeshService->RebuildDirtyChunks(world.BlockWorld, *world.BlockRegistry,
                                          warmup_budget.MaxMeshDrain,
                                          warmup_budget.MaxMeshSchedule);
    world.MeshService->DrainAsyncMeshResults(world.BlockWorld, *world.BlockRegistry,
                                             warmup_budget.MaxMeshDrain);
    if (!world.MeshService->HasPendingDirty() &&
        !world.MeshService->HasPendingAsyncMeshWork())
    {
      break;
    }
  }

  world.MeshService->WaitForAsyncMeshIdle();
  while (world.MeshService->HasPendingDirty())
  {
    world.MeshService->RebuildDirtyChunks(world.BlockWorld, *world.BlockRegistry,
                                          warmup_budget.MaxMeshDrain,
                                          warmup_budget.MaxMeshSchedule);
    world.MeshService->DrainAsyncMeshResults(world.BlockWorld, *world.BlockRegistry,
                                             warmup_budget.MaxMeshDrain);
  }

  world.MaxLoadOpsPerFrame = prev_load_ops;
  world.RefreshStreamerSettings();
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
        if (Streamer)
        {
          Streamer->NotifyChunkCommitted(coord);
        }
        else
        {
          world.GetMeshService().MarkDirty(coord);
        }
        ChunkPhysicsSeedBudgets seed_budgets;
        SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (SealFluidShoreOnChunkCommitted(
                world.BlockWorld, *world.BlockRegistry, settings,
                world.WorldgenOwnerPackId, coord))
        {
          const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE;
          world.MarkTerrainChunkMeshDirty(glm::ivec3(coord.x, 0, coord.z), 0,
                                          remesh_max_y);
        }
      });
  ChunkScheduler->SetColumnMeshDirtyFn(
      [&world](glm::ivec3 groundCoord, int min_y, int max_y)
      {
        world.MarkTerrainChunkMeshDirty(groundCoord, min_y, max_y);
      });
  world.Persistence->EnsureChunkIoInitialized();
}

void UWorldStreaming::TickAsyncChunkSystems(UWorld &world)
{
  const ProceduralSettings &procedural = world.GetProceduralSettings();
  EmergeCoordinator->BeginFrame(procedural, world.LastMovementSpeed,
                                world.MaxLoadOpsPerFrame);
  const UChunkEmergeCoordinator::FrameBudget budget =
      EmergeCoordinator->GetLastBudget();
  if (ChunkScheduler && procedural.AsyncChunkGeneration)
  {
    ChunkScheduler->Tick(world.BlockWorld, budget.MaxChunkCommits,
                         budget.MaxLoadOps);
  }
  world.Persistence->TickAsyncChunkIo(world);
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
          world.RelightTerrainColumn(coord.x, coord.z, 0, settings.MaxHeight);
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
        }
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
        ChunkGenTokens.Bump(ground);
        if (ChunkScheduler)
        {
          ChunkScheduler->Invalidate(ground);
        }
      },
      [&world](glm::ivec3 coord)
      {
        world.MarkTerrainChunkMeshDirty(glm::ivec3(coord.x, 0, coord.z), 0,
                                        world.GetProceduralSettings().MaxHeight);
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
      const StreamingAltitudeSnapshot alt = ComputeStreamingAltitude(
          renderDistanceChunks, eye.y, cap.feetY(eye),
          render.DistanceFogStartRatio, altitudeParams);
      effectiveRenderDistance = alt.EffectiveRenderDistance;
      effectiveFogStartRatio = alt.EffectiveFogStartRatio;
    }
    else
    {
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
