#include "World/Core/WorldCooperativeOps.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Core/RuntimeTuning.h"
#include "App/Platform/Log.h"
#include "glog/logging.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/ChunkRenderDemand.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/WorldStreaming.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "Render/Mesh/FluidColumnSummary.h"
#include "Core/Jobs/JobThreadPool.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/IULightingPipeline.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"
#include "World/Persistence/WorldPersistence.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>

namespace cutum
{

namespace
{

bool IsWorldColumnSourceTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
  return value && value[0] == '1';
}

void ParkSpawnRingMeshWhileRelightDeferred(UWorld &world)
{
  if (!world.IsLightingRelightDeferred())
  {
    return;
  }
  const auto &phys = world.GetPhysicsTelemetry();
  if (ShouldSkipParkSpawnRingForMissHeal(
          world.NeedsSpawnRingCatchUp(), phys.FocusMissingMesh != 0,
          phys.MissHoriz, world.IsEnterSessionActive()))
  {
    return;
  }
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
  world.GetMeshService().ParkDirtyWithinHorizontalRadius(
      focus, EnterVisualWorkRadiusChunks());
}

} // namespace

struct CooperativeParallelGenState
{
  CooperativeParallelGenState()
      : Pool(ComputeWorkerThreadCount(JobPoolKind::CoopGeneration),
             "CoopGeneration")
  {
  }

  std::unique_ptr<UPipelineChunkPopulator> Populator;
  UObjectLibrary *PopulatorObjectLibrary{nullptr};
  UJobThreadPool Pool;
  UCompletedJobQueue<ChunkPopulateResult> Completed;
  size_t InFlight{0};
};

namespace
{

// Load-path weights tuned for saved worlds (e.g. World_091): mesh build dominates,
// relight second, chunk IO/metadata are short slices of wall-clock time.
constexpr float kPhaseWeightMetadata = 0.01f;
constexpr float kPhaseWeightEntities = 0.01f;
constexpr float kPhaseWeightChunks = 0.12f;
constexpr float kPhaseWeightSpatial = 0.04f;
constexpr float kPhaseWeightRelight = 0.18f;
constexpr float kPhaseWeightMeshWarmup = 0.58f;
constexpr float kPhaseWeightPrepareView = 0.06f;

// Rare empty-world path: procedural generation replaces chunk IO budget.
constexpr float kProceduralFillWeightGenerate = 0.28f;
constexpr float kProceduralFillWeightMeshWarmup = 0.46f;

// Create-world path: column generation then relight/mesh.
constexpr float kCreateWeightInit = 0.02f;
constexpr float kCreateWeightGenerate = 0.30f;
constexpr float kCreateWeightRelight = 0.16f;
constexpr float kCreateWeightMeshWarmup = 0.48f;
constexpr float kCreateWeightPrepare = 0.04f;

constexpr int kMeshWarmupMaxTicks = 50000;
/// Large saved worlds can sit in MeshWarmup at low FPS; tick-only timeout never
/// fires before flight-sim safety wall. Cap wall time so EnterGame can proceed.
constexpr int kMeshWarmupMaxWallMs = 75000;
/// Hidden-window flight-sim: RelightColumns can stall >2min on World_164.
constexpr int kRelightColumnsMaxWallMs = 60000;
constexpr int kStreamUnloadMarginChunks = 1;

glm::ivec3 ResolveSpatialLoadCenter(const UWorld &world)
{
  return UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
}

bool UseParallelChunkGeneration(const UWorld &world)
{
  return world.GetProceduralSettings().AsyncChunkGeneration &&
         std::thread::hardware_concurrency() > 1;
}

bool CooperativeTerrainMeshesIncompleteInPatch(const UWorld &world,
                                             int patch_min_x, int patch_max_x,
                                             int patch_min_z, int patch_max_z);

bool CooperativeTerrainMeshesIncomplete(const UWorld &world, int patch_min_x,
                                        int patch_max_x, int patch_min_z,
                                        int patch_max_z)
{
  if (CooperativeTerrainMeshesIncompleteInPatch(world, patch_min_x, patch_max_x,
                                                patch_min_z, patch_max_z))
  {
    return true;
  }
  const glm::ivec3 spawn_chunk =
      UChunkManager::WorldToChunk(WorldPosToBlock(world.GetSpawnPoint()));
  const glm::ivec3 spawn_ground(spawn_chunk.x, 0, spawn_chunk.z);
  const int spawn_radius = world.GetRenderDistanceChunks() + 1;
  if (world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), spawn_ground, spawn_radius))
  {
    return true;
  }
  return world.GetMeshService().HasDirtyWithinHorizontalRadius(spawn_ground,
                                                               spawn_radius);
}

bool CooperativeTerrainMeshesIncompleteInPatch(const UWorld &world,
                                             int patch_min_x, int patch_max_x,
                                             int patch_min_z, int patch_max_z)
{
  const ProceduralSettings &settings = world.GetProceduralSettings();
  const int sea = settings.SeaLevel;
  const int cy0 = std::max(
      FloorDiv(settings.MinHeight, CHUNK_SIZE),
      FloorDiv(sea - CHUNK_SIZE, CHUNK_SIZE));
  const int cy1 = std::min(FloorDiv(settings.MaxHeight, CHUNK_SIZE),
                           FloorDiv(sea + CHUNK_SIZE * 2, CHUNK_SIZE));
  const int min_cx = FloorDiv(patch_min_x, CHUNK_SIZE);
  const int max_cx = FloorDiv(patch_max_x, CHUNK_SIZE);
  const int min_cz = FloorDiv(patch_min_z, CHUNK_SIZE);
  const int max_cz = FloorDiv(patch_max_z, CHUNK_SIZE);
  for (int cx = min_cx; cx <= max_cx; ++cx)
  {
    for (int cz = min_cz; cz <= max_cz; ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        const glm::ivec3 coord(cx, cy, cz);
        if (!world.GetBlockWorld().GetChunkManager().HasChunk(coord))
        {
          continue;
        }
        if (!world.GetMeshService().HasGreedyMesh(coord))
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool IsCreateSpawnWarmupSettled(const UWorld &world)
{
  return world.IsCreateSpawnWarmupSettled();
}

void TickCreateSpawnMeshWarmup(UWorld &world, int budget)
{
  world.DrainSpawnRadiusMeshWarmup(budget);
  const int emerge_budget = std::max(1, budget / 2);
  if (world.IsEnterLitGateActive())
  {
    world.TickEnterGateMeshDrain(emerge_budget);
  }
  else
  {
    world.TickEnterStreamingWarmup(emerge_budget);
  }
}

static_assert(kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks +
                  kPhaseWeightSpatial + kPhaseWeightRelight +
                  kPhaseWeightMeshWarmup + kPhaseWeightPrepareView ==
              1.0f,
              "load progress weights must sum to 1");

static_assert(kCreateWeightInit + kCreateWeightGenerate + kCreateWeightRelight +
                  kCreateWeightMeshWarmup + kCreateWeightPrepare == 1.0f,
              "create progress weights must sum to 1");

float CooperativeLoadProgressBase()
{
  return kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks +
         kPhaseWeightSpatial;
}

float CooperativeLoadProgressAfterMesh()
{
  return CooperativeLoadProgressBase() + kPhaseWeightRelight +
         kPhaseWeightMeshWarmup;
}

float CooperativeLoadMeshProgressBase()
{
  return CooperativeLoadProgressBase() + kPhaseWeightRelight;
}

float ProceduralFillProgressBase()
{
  return kPhaseWeightMetadata + kPhaseWeightEntities;
}

float ProceduralFillProgressAfterGenerate()
{
  return ProceduralFillProgressBase() + kProceduralFillWeightGenerate;
}

float ProceduralFillMeshProgressBase()
{
  return ProceduralFillProgressAfterGenerate() + kPhaseWeightRelight;
}

float CooperativeCreateMeshProgressBase()
{
  return kCreateWeightInit + kCreateWeightGenerate + kCreateWeightRelight;
}

float ComputeRelightLoadProgress(float progress_base, float relight_weight,
                                 size_t chunk_index, size_t chunk_total,
                                 size_t column_done, size_t column_total,
                                 int async_in_flight)
{
  const float chunk_progress =
      chunk_total == 0
          ? 1.0f
          : static_cast<float>(chunk_index) / static_cast<float>(chunk_total);
  const float chunk_part = 0.4f * std::min(1.0f, chunk_progress);

  float column_progress = 1.0f;
  if (column_total > 0)
  {
    column_progress =
        (static_cast<float>(column_done) +
         0.5f * static_cast<float>(async_in_flight)) /
        static_cast<float>(column_total);
    column_progress = std::min(1.0f, column_progress);
  }
  else if (chunk_total > 0 && chunk_index < chunk_total)
  {
    column_progress = 0.0f;
  }
  const float column_part = 0.6f * column_progress;
  return progress_base + relight_weight * (chunk_part + column_part);
}

void MarkSpawnAreaPreparedAfterCooperativeLoad(UWorld &world,
                                              WorldCoopKind kind)
{
  if (kind != WorldCoopKind::Load)
  {
    return;
  }
  // PrepareView already required spawn-ring Presentable + vis. Always mark so
  // GpuWarmup is upload-only (do not wait on post-EndEnterLitGate live debt).
  world.MarkSpawnAreaPreparedByCooperativeLoad();
}

void FinalizeCooperativeLoadForEnterGame(UWorld &world, WorldCoopKind kind)
{
  if (kind == WorldCoopKind::Load)
  {
    MarkSpawnAreaPreparedAfterCooperativeLoad(world, kind);
  }
}

void NoteColumnLightCalculationSettled(UWorld &world,
                                       glm::ivec2 column_block_xz,
                                       int min_world_y, int max_world_y)
{
  if (max_world_y < min_world_y)
  {
    return;
  }
  const glm::ivec3 ground = UChunkManager::WorldToChunk(
      glm::ivec3(column_block_xz.x, 0, column_block_xz.y));
  const ProceduralSettings &settings = world.GetProceduralSettings();
  const int cy0 = std::max(FloorDiv(settings.MinHeight, CHUNK_SIZE),
                           FloorDiv(std::max(settings.MinHeight, min_world_y),
                                    CHUNK_SIZE));
  const int cy1 = std::min(FloorDiv(settings.MaxHeight, CHUNK_SIZE),
                           FloorDiv(std::min(settings.MaxHeight, max_world_y),
                                    CHUNK_SIZE));
  UChunkManager &chunks = world.GetBlockWorld().GetChunkManager();
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 coord(ground.x, cy, ground.z);
    if (chunks.HasChunk(coord))
    {
      world.NoteChunkSliceLightCalculationSettled(coord);
    }
  }
}

} // namespace

UWorldCooperativeSession::~UWorldCooperativeSession()
{
  delete ParallelGen;
  ParallelGen = nullptr;
}

void UWorldCooperativeSession::BeginDeferredRelightQueue(UWorld &world)
{
  world.CancelAsyncRelightWork();
  if (world.Persistence)
  {
    world.Persistence->ClearPendingRelights(
        "cooperative_deferred_relight_start");
  }
  BeginColumnRelightQueue(world);
}

void UWorldCooperativeSession::BeginBulkChunkRelightQueue(UWorld &world)
{
  Relight.BulkRelightChunkQueue.clear();
  Relight.BulkRelightChunkScheduledIndex = 0;
  Relight.BulkRelightChunkAppliedCount = 0;
  Relight.ColumnRelightQueue.clear();
  Relight.ColumnRelightIndex = 0;
  Relight.ColumnRelightScheduledIndex = 0;
  Relight.ColumnRelightAppliedCount = 0;
  Relight.RelightQueue.clear();
  Relight.RelightQueueIndex = 0;

  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      { Relight.BulkRelightChunkQueue.push_back(chunk.GetCoord()); });

  world.SetLightingRelightDeferred(false);
  world.SetLightingSkylightBulkComplete(true);

  std::cout << "[WorldLoad] RelightBulkChunks: "
            << Relight.BulkRelightChunkQueue.size() << " chunks" << std::endl;

  if (Relight.BulkRelightChunkQueue.empty())
  {
    BeginEmissiveBlockLightQueue(world);
    return;
  }

  CurrentPhase = Phase::RelightBulkChunks;
}

void UWorldCooperativeSession::BeginColumnRelightQueue(UWorld &world)
{
  Relight.ColumnRelightQueue.clear();
  Relight.ColumnRelightIndex = 0;
  Relight.ColumnRelightScheduledIndex = 0;
  Relight.ColumnRelightAppliedCount = 0;

  std::unordered_set<glm::ivec3, IVec3Hash> ground_columns;
  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        const glm::ivec3 ground(coord.x, 0, coord.z);
        if (ground_columns.insert(ground).second)
        {
          Relight.ColumnRelightQueue.push_back(
              glm::ivec2(ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE));
        }
      });

  world.SetLightingRelightDeferred(false);
  world.SetLightingSkylightBulkComplete(true);
  Relight.RelightQueue.clear();
  Relight.RelightQueueIndex = 0;

  std::cout << "[WorldLoad] RelightColumns: " << Relight.ColumnRelightQueue.size()
            << " terrain columns" << std::endl;

  if (Relight.ColumnRelightQueue.empty())
  {
    BeginEmissiveBlockLightQueue(world);
    return;
  }

  Relight.RelightColumnsStartedAt = std::chrono::steady_clock::now();
  CurrentPhase = Phase::RelightColumns;
}

void UWorldCooperativeSession::FinishEmissiveBlockLightRelight(UWorld &world)
{
  std::cout << "[WorldLoad] RelightEmissiveBlockLight done: "
            << Relight.EmissiveChunkRelightQueue.size() << " chunks" << std::endl;
  Relight.EmissiveChunkRelightQueue.clear();
  Relight.EmissiveChunkRelightIndex = 0;
  if (Kind == WorldCoopKind::Create && !PrepareCreateVisuals)
  {
    FinishHeadlessCreate(world);
    return;
  }
  world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
  BeginMeshWarmupInner(world);
}

void UWorldCooperativeSession::FinishHeadlessCreate(UWorld &world)
{
  world.SetLightingRelightDeferred(false);
  world.SetLightingSkylightBulkComplete(false);
  world.BlockCounter.MarkNeedsRecount();

  bool has_terrain_chunks = false;
  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &) { has_terrain_chunks = true; });
  world.BlockWorldReady = has_terrain_chunks || world.CachedBlockCount > 0;
  CurrentPhase = Phase::Done;
  Active = false;
}

void UWorldCooperativeSession::BeginEmissiveBlockLightQueue(UWorld &world)
{
  Relight.EmissiveChunkRelightQueue.clear();
  Relight.EmissiveChunkRelightIndex = 0;
  Relight.ColumnRelightQueue.clear();
  Relight.ColumnRelightIndex = 0;
  Relight.ColumnRelightScheduledIndex = 0;
  Relight.ColumnRelightAppliedCount = 0;

  world.CancelAsyncRelightWork();

  // On create/load we still need emissive block light to be correct near the
  // player. Full-world scans can be very expensive, so we only scan near the
  // spawn/focus radius.
  const bool coop_create_or_load =
      Kind == WorldCoopKind::Create || Kind == WorldCoopKind::Load;
  const glm::ivec3 focus_chunk =
      UChunkManager::WorldToChunk(WorldPosToBlock(world.GetSpawnPoint()));
  const glm::ivec3 focus_ground(focus_chunk.x, 0, focus_chunk.z);
  const int focus_radius = world.GetRenderDistanceChunks() + 1;

  if (world.BlockRegistry)
  {
    // UX U2/S4: the cheap radius filter builds the candidate list here; the
    // per-chunk 16^3 emissive scan moved into the phase tick (batched),
    // where a focus-radius burst of 10-25M block reads used to run in one
    // tick.
    world.BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        {
          if (coop_create_or_load)
          {
            const glm::ivec3 coord = chunk.GetCoord();
            const int dx = std::abs(coord.x - focus_ground.x);
            const int dz = std::abs(coord.z - focus_ground.z);
            if (std::max(dx, dz) > focus_radius)
            {
              return;
            }
          }
          Relight.EmissiveChunkScanQueue.push_back(chunk.GetCoord());
        });
  }

  std::cout << "[WorldLoad] RelightEmissiveBlockLight: "
            << Relight.EmissiveChunkRelightQueue.size() << " chunks" << std::endl;

  if (Relight.EmissiveChunkRelightQueue.empty())
  {
    if (Kind == WorldCoopKind::Create && !PrepareCreateVisuals)
    {
      FinishHeadlessCreate(world);
      return;
    }
    world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
    BeginMeshWarmupInner(world);
    return;
  }

  CurrentPhase = Phase::RelightEmissiveBlockLight;
}

void UWorldCooperativeSession::BeginMeshWarmupInner(UWorld &world)
{
  // Skylight-only bulk relight ends here; gameplay edits/streaming need block light.
  world.SetLightingSkylightBulkComplete(false);
  const size_t pending_now =
      world.MeshService->GetDirtyCount() +
      static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
  if (MeshWarmup.MeshWarmupFinalizeOnly)
  {
    // Era51: continue residual drain — do not MarkAllDirty the whole world again.
    if (MeshWarmup.MeshWarmupTicks == 0)
    {
      MeshWarmup.MeshWarmupStartedAt = std::chrono::steady_clock::now();
      MeshWarmup.MeshWarmupStartPending = std::max<size_t>(1, pending_now);
      MeshWarmup.MeshWarmupSeenTotal = MeshWarmup.MeshWarmupStartPending;
      MeshWarmup.MeshWarmupCompletedTotal = 0;
      MeshWarmup.MeshWarmupProcessedMax = 0;
      std::cout << "[WorldLoad] MeshWarmup finalize: pending=" << pending_now
                << std::endl;
    }
    CurrentPhase = Phase::MeshWarmup;
    return;
  }
  world.BlockCounter.MarkNeedsRecount();
  world.MeshService->CancelAsyncInFlightKeepDirty();
  bool has_chunks = false;
  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &) { has_chunks = true; });
  if (has_chunks)
  {
    world.MeshService->GetCache().MarkAllDirtyFromWorld(world.BlockWorld, false);
    if (Kind == WorldCoopKind::Create)
    {
      const ProceduralSettings &settings = world.GetProceduralSettings();
      const int remesh_min_y =
          std::max(settings.MinHeight, settings.SeaLevel - CHUNK_SIZE);
      const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
      const int min_cx = FloorDiv(Gen.GenPatchMinX, CHUNK_SIZE);
      const int max_cx = FloorDiv(Gen.GenPatchMaxX, CHUNK_SIZE);
      const int min_cz = FloorDiv(Gen.GenPatchMinZ, CHUNK_SIZE);
      const int max_cz = FloorDiv(Gen.GenPatchMaxZ, CHUNK_SIZE);
      for (int cx = min_cx; cx <= max_cx; ++cx)
      {
        for (int cz = min_cz; cz <= max_cz; ++cz)
        {
          world.MarkTerrainChunkMeshDirty(glm::ivec3(cx, 0, cz), remesh_min_y,
                                          remesh_max_y);
        }
      }
    }
  }
  if (Kind == WorldCoopKind::Load)
  {
    ParkSpawnRingMeshWhileRelightDeferred(world);
  }
  MeshWarmup.MeshWarmupTicks = 0;
  MeshWarmup.MeshWarmupStartedAt = {};
  MeshWarmup.MeshWarmupProcessedMax = 0;
  MeshWarmup.MeshWarmupCompletedTotal = 0;
  MeshWarmup.MeshWarmupStartPending =
      world.MeshService->GetDirtyCount() +
      static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
  if (MeshWarmup.MeshWarmupStartPending == 0)
  {
    MeshWarmup.MeshWarmupStartPending = 1;
  }
  MeshWarmup.MeshWarmupSeenTotal = MeshWarmup.MeshWarmupStartPending;
  CurrentPhase = Phase::MeshWarmup;
}

void UWorldCooperativeSession::BeginMeshWarmup(UWorld &world)
{
  if (world.IsLightingRelightDeferred() && world.BlockRegistry)
  {
    BeginDeferredRelightQueue(world);
    return;
  }
  BeginMeshWarmupInner(world);
}

void UWorldCooperativeSession::BeginPrepareEnter()
{
  CurrentPhase = Phase::PrepareEnter;
}

void UWorldCooperativeSession::ReportMeshWarmupStart(IUProgressSink &sink) const
{
  if (CurrentPhase != Phase::MeshWarmup)
  {
    return;
  }
  const float mesh_base =
      Kind == WorldCoopKind::Create
          ? CooperativeCreateMeshProgressBase()
          : (ProceduralFillLoadPath ? ProceduralFillMeshProgressBase()
                                    : CooperativeLoadMeshProgressBase());
  Report(sink, "mesh_warmup", mesh_base,
         "Building terrain meshes... 0/" +
             std::to_string(MeshWarmup.MeshWarmupStartPending),
         0, static_cast<int>(MeshWarmup.MeshWarmupStartPending));
}

const char *UWorldCooperativeSession::PhaseId() const
{
  switch (CurrentPhase)
  {
  case Phase::Init:
    return "init";
  case Phase::Metadata:
    return "metadata";
  case Phase::Entities:
    return "entities";
  case Phase::ScanChunks:
    return "scan_chunks";
  case Phase::LoadChunks:
    return "load_chunks";
  case Phase::SpatialChunks:
    return "spatial_chunks";
  case Phase::RelightChunks:
    return "relight_chunks";
  case Phase::RelightColumns:
    return "relight_columns";
  case Phase::RelightBulkChunks:
    return "relight_bulk_chunks";
  case Phase::RelightEmissiveBlockLight:
    return "relight_emissive_blocklight";
  case Phase::MeshWarmup:
    return "mesh_warmup";
  case Phase::PrepareEnter:
    return "prepare_enter";
  case Phase::PrepareView:
    return "prepare_view";
  case Phase::PostLoadAnalysis:
    return "post_load_analysis";
  case Phase::ProceduralFill:
    return "procedural_fill";
  case Phase::FinalizeWorld:
    return "finalize_world";
  case Phase::ScanSaveChunks:
    return "scan_save_chunks";
  case Phase::DrainAsyncIo:
    return "drain_async_io";
  case Phase::SaveChunks:
    return "save_chunks";
  case Phase::SaveMetadataMarker:
  case Phase::SaveMetadataUsers:
  case Phase::SaveMetadataCreatures:
  case Phase::SaveMetadataWorldData:
  case Phase::SaveMetadataDiag:
    return "save_metadata";
  case Phase::GenerateColumns:
    return "generate_columns";
  case Phase::ResolveSpawn:
    return "resolve_spawn";
  case Phase::PostCreate:
    return "post_create";
  case Phase::Done:
    return "done";
  }
  return "unknown";
}

void UWorldCooperativeSession::Report(IUProgressSink &sink,
                                      const std::string &phaseId, float fraction,
                                      const std::string &message, int subDone,
                                      int subTotal) const
{
  sink.Report(phaseId, fraction, message, subDone, subTotal);
}

void UWorldCooperativeSession::RecordPhaseTiming(const UWorld &world)
{
  const auto now = std::chrono::steady_clock::now();
  const std::string current_phase = PhaseId();
  if (TimedPhaseId.empty())
  {
    TimedPhaseId = current_phase;
    OperationStartedAt = now;
    PhaseStartedAt = now;
    return;
  }
  if (current_phase == TimedPhaseId)
  {
    return;
  }

  const double phase_ms = std::chrono::duration<double, std::milli>(
                              now - PhaseStartedAt)
                              .count();
  const double operation_ms = std::chrono::duration<double, std::milli>(
                                  now - OperationStartedAt)
                                  .count();
  const char *kind = Kind == WorldCoopKind::Load
                         ? "load"
                         : (Kind == WorldCoopKind::Create ? "create" : "save");
  const ProceduralSettings &settings = world.GetProceduralSettings();
  const bool has_cooperative_generation =
      Kind == WorldCoopKind::Create || ProceduralFillLoadPath;
  const size_t generation_workers =
      has_cooperative_generation
          ? (settings.AsyncChunkGeneration
                 ? ComputeWorkerThreadCount(JobPoolKind::CoopGeneration)
                 : 1)
          : 0;
  const int spatial_total = Spatial.SpatialRadius > 0
                                ? (2 * Spatial.SpatialRadius + 1) *
                                      (2 * Spatial.SpatialRadius + 1)
                                : 0;

  std::string line =
      std::string("kind=") + kind + " phase=" + TimedPhaseId +
      " phase_ms=" + std::to_string(phase_ms) +
      " operation_ms=" + std::to_string(operation_ms) +
      " chunk_files_seen=" + std::to_string(ChunkFiles.size()) +
      " chunk_files_read=" + std::to_string(ChunkFilesRead) +
      " voxels_from_files=" + std::to_string(VoxelsFromChunkFiles) +
      " spatial_columns=" + std::to_string(Spatial.SpatialColumnsVisited) + "/" +
      std::to_string(spatial_total) + " generation_columns=" +
      std::to_string(Gen.GenDoneColumns) + "/" + std::to_string(Gen.GenTotalColumns) +
      " async_generation=" + (settings.AsyncChunkGeneration ? "1" : "0") +
      " generation_workers=" + std::to_string(generation_workers) +
      " async_chunk_io=" + (settings.AsyncChunkIo ? "1" : "0");
  CubatariumLogInfo("WorldLoadPerf", line);
  std::cerr << "[WorldLoadPerf] " << line << std::endl;

  // UX U4 watchdog: a phase whose AVERAGE tick exceeded 250ms still means a
  // frozen bar for its duration (the frame loop pumps between ticks, so the
  // per-tick cost is what the user feels).
  if (const long long total_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                PhaseStartedAt)
              .count();
      total_ms > 250)
  {
    const std::string slow_line =
        std::string("phase=") + TimedPhaseId + " total_ms=" +
        std::to_string(total_ms);
    CubatariumLogInfo("WorldLoadPerf", "slow-phase " + slow_line);
    std::cerr << "[WorldLoadPerf][slow-phase] " << slow_line << std::endl;
  }

  TimedPhaseId = current_phase;
  PhaseStartedAt = now;
}

bool UWorldCooperativeSession::ForceCapEnterGameVisual(UWorld &world,
                                                       IUProgressSink &sink)
{
  if (!Active || Kind != WorldCoopKind::Load)
  {
    return false;
  }
  if (CurrentPhase == Phase::Done)
  {
    return true;
  }
  world.FinalizePlayerAfterWorldLoad();
  if (auto user = world.GetCurrentUser())
  {
    world.ApplyUserToCamera(user);
  }
  else
  {
    world.ApplySpawnToCamera();
  }
  world.WarmupVisibleListAtCamera();
  FinalizeCooperativeLoadForEnterGame(world, Kind);
  CurrentPhase = Phase::Done;
  Active = false;
  StreamingWarmup.StreamingWarmupTicks = 0;
  StreamingWarmup.StreamingWarmupPeakDebt = 0;
  Report(sink, "done", 1.f, "World loaded.");
  RecordPhaseTiming(world);
  return true;
}

void UWorldCooperativeSession::Cancel()
{
  Active = false;
  CurrentPhase = Phase::Done;
  CancelBackgroundWorkers();
}

void UWorldCooperativeSession::CancelBackgroundWorkers()
{
  if (ParallelGen)
  {
    ParallelGen->Pool.CancelPendingJobs();
    (void)ParallelGen->Completed.DrainAll();
    ParallelGen->InFlight = 0;
  }
  // A35 R0 / A31 P4: join fluid summary worker on coop cancel / world switch.
  (void)ShutdownFluidSummaryWorker(2000);
}

bool UWorldCooperativeSession::BlocksStreamingTick() const
{
  if (!Active)
  {
    return false;
  }
  if (Kind == WorldCoopKind::Save)
  {
    return true;
  }
  switch (CurrentPhase)
  {
  case Phase::PrepareView:
  case Phase::PrepareEnter:
  case Phase::Done:
    return false;
  default:
    return true;
  }
}

bool UWorldCooperativeSession::IsEnterVisualWarmupActive() const
{
  if (!Active || Kind != WorldCoopKind::Load)
  {
    return false;
  }
  return CurrentPhase >= Phase::MeshWarmup &&
         CurrentPhase <= Phase::PrepareView;
}

void UWorldCooperativeSession::BeginLoad(UWorld &world,
                                         const std::string &world_folder_path)
{
  const ProceduralSettings &runtime_settings = world.GetProceduralSettings();
  const bool runtime_async_chunk_generation =
      runtime_settings.AsyncChunkGeneration;
  const bool runtime_async_chunk_io = runtime_settings.AsyncChunkIo;
  const int runtime_max_chunk_commits_per_frame =
      runtime_settings.MaxChunkCommitsPerFrame;
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Load;
  Active = true;
  RuntimeAsyncChunkGeneration = runtime_async_chunk_generation;
  RuntimeAsyncChunkIo = runtime_async_chunk_io;
  RuntimeMaxChunkCommitsPerFrame = runtime_max_chunk_commits_per_frame;
  FolderPath = world_folder_path;
  CurrentPhase = Phase::Init;
  TimedPhaseId = "init";
  OperationStartedAt = std::chrono::steady_clock::now();
  PhaseStartedAt = OperationStartedAt;
  (void)world;
}

void UWorldCooperativeSession::BeginSave(UWorld &world,
                                         const std::string &world_folder_path,
                                         bool resume_streaming_after_save)
{
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Save;
  Active = true;
  ResumeStreamingAfterSave = resume_streaming_after_save;
  FolderPath = world_folder_path;
  CurrentPhase = Phase::Init;
  TimedPhaseId = "init";
  OperationStartedAt = std::chrono::steady_clock::now();
  PhaseStartedAt = OperationStartedAt;
  (void)world;
}

void UWorldCooperativeSession::BeginCreate(UWorld &world,
                                           const std::string &world_name,
                                           const bool prepare_visuals)
{
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Create;
  Active = true;
  PrepareCreateVisuals = prepare_visuals;
  TargetWorldName = world_name;
  CurrentPhase = Phase::Init;
  TimedPhaseId = "init";
  OperationStartedAt = std::chrono::steady_clock::now();
  PhaseStartedAt = OperationStartedAt;
  (void)world;
}

bool UWorldCooperativeSession::ScanChunkFilesBatch(UWorld &world,
                                                   double maxWallMs)
{
  (void)world;
  if (!ScanChunksActive)
  {
    return true;
  }
  const auto batch_start = std::chrono::steady_clock::now();
  for (const auto &entry : std::filesystem::directory_iterator(ScanChunksDir))
  {
    const auto ext = entry.path().extension();
    if (ext != ".json" && ext != ".cchunk")
    {
      continue;
    }
    ScanChunksBatch.push_back(entry.path());
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - batch_start)
            .count();
    if (elapsed_ms >= maxWallMs)
    {
      return false; // more batches remain
    }
  }
  return true; // directory exhausted
}

void UWorldCooperativeSession::ScanChunkFiles(UWorld &world)
{
  ChunkFiles.clear();
  ChunkFileIndex = 0;
  const std::string chunks_dir = FolderPath + "/chunks";
  if (!Spatial.SpatialStreamingLoad && std::filesystem::exists(chunks_dir))
  {
    // S8: drain the directory in wall-budgeted batches; a huge worlds dir
    // no longer sweeps in one tick (typical cost tens of ms per 10k files).
    ScanChunksDir = chunks_dir;
    ScanChunksBatch.clear();
    ScanChunksActive = true;
    bool done = false;
    while (!done)
    {
      done = ScanChunkFilesBatch(world, 16.0);
    }
    ScanChunksActive = false;
    ChunkFiles = std::move(ScanChunksBatch);
    ScanChunksBatch.clear();
    std::sort(ChunkFiles.begin(), ChunkFiles.end());
  }
  (void)world;
}

void UWorldCooperativeSession::ScanSaveChunkCoords(UWorld &world)
{
  Save.SaveChunkCoords.clear();
  Save.SaveChunkIndex = 0;
  Save.SaveUsesTerrainColumns = true;
  std::unordered_set<glm::ivec3, IVec3Hash> grounds;
  const bool incremental_save =
      world.IsStreamingEnabled() && world.HasPersistedSave;
  if (incremental_save || !world.ModifiedChunks.empty())
  {
    if (incremental_save)
    {
      world.BlockWorld.GetChunkManager().ForEachChunk(
          [&](const UChunk &chunk)
          {
            const glm::ivec3 coord = chunk.GetCoord();
            grounds.insert(glm::ivec3(coord.x, 0, coord.z));
          });
    }
    for (const glm::ivec3 &modified : world.ModifiedChunks)
    {
      grounds.insert(glm::ivec3(modified.x, 0, modified.z));
    }
  }
  else
  {
    world.BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        {
          const glm::ivec3 coord = chunk.GetCoord();
          grounds.insert(glm::ivec3(coord.x, 0, coord.z));
        });
  }
  Save.SaveChunkCoords.reserve(grounds.size());
  for (const glm::ivec3 &ground : grounds)
  {
    Save.SaveChunkCoords.push_back(ground);
  }
}

void UWorldCooperativeSession::InitGenerationGrid(UWorld &world,
                                                  const bool center_on_load_focus)
{
  if (!world.WorldGen)
  {
    world.RebuildWorldGenPipeline();
  }
  else if (world.ObjectLibrary && world.BlockRegistry)
  {
    world.ObjectLibrary->RebindBlockIds(*world.BlockRegistry);
  }
  if (center_on_load_focus)
  {
    const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
    Gen.GenCenterX = focus_block.x;
    Gen.GenCenterZ = focus_block.z;
  }
  else
  {
    Gen.GenCenterX = 0;
    Gen.GenCenterZ = 0;
  }
  const int collision_shell_chunks =
      world.PhysicsFlags.EnableCollisionReadinessGate
          ? std::max(1, world.PhysicsBudgetConfig.CollisionSafetyRadiusChunks)
          : 1;
  const int patch_radius_chunks =
      std::max(1, world.RenderDistanceChunks) + collision_shell_chunks +
      kStreamUnloadMarginChunks;
  const int patch_radius_blocks = patch_radius_chunks * CHUNK_SIZE;
  const int half = patch_radius_blocks;
  const int min_x = Gen.GenCenterX - half;
  const int max_x = Gen.GenCenterX + half;
  const int min_z = Gen.GenCenterZ - half;
  const int max_z = Gen.GenCenterZ + half;
  Gen.GenPatchMinX = min_x;
  Gen.GenPatchMaxX = max_x;
  Gen.GenPatchMinZ = min_z;
  Gen.GenPatchMaxZ = max_z;

  Gen.GenColumnQueue.clear();
  Gen.GenColumnQueue.reserve(static_cast<size_t>((max_x - min_x + 1) *
                                             (max_z - min_z + 1)));
  for (int x = min_x; x <= max_x; ++x)
  {
    for (int z = min_z; z <= max_z; ++z)
    {
      Gen.GenColumnQueue.push_back({x, z});
    }
  }
  std::sort(Gen.GenColumnQueue.begin(), Gen.GenColumnQueue.end(),
            [this](const GenColumnEntry &a, const GenColumnEntry &b)
            {
              const int da = (a.X - Gen.GenCenterX) * (a.X - Gen.GenCenterX) +
                             (a.Z - Gen.GenCenterZ) * (a.Z - Gen.GenCenterZ);
              const int db = (b.X - Gen.GenCenterX) * (b.X - Gen.GenCenterX) +
                             (b.Z - Gen.GenCenterZ) * (b.Z - Gen.GenCenterZ);
              return da < db;
            });

  Gen.GenColumnIndex = 0;
  Gen.GenTotalColumns = static_cast<int>(Gen.GenColumnQueue.size());
  Gen.GenDoneColumns = 0;

  Gen.GenChunkQueue.clear();
  Gen.GenChunkScheduleIndex = 0;
  delete ParallelGen;
  ParallelGen = nullptr;
  const int min_cx = FloorDiv(min_x, CHUNK_SIZE);
  const int max_cx = FloorDiv(max_x, CHUNK_SIZE);
  const int min_cz = FloorDiv(min_z, CHUNK_SIZE);
  const int max_cz = FloorDiv(max_z, CHUNK_SIZE);
  Gen.GenChunkQueue.reserve(static_cast<size_t>((max_cx - min_cx + 1) *
                                            (max_cz - min_cz + 1)));
  for (int cx = min_cx; cx <= max_cx; ++cx)
  {
    for (int cz = min_cz; cz <= max_cz; ++cz)
    {
      Gen.GenChunkQueue.push_back({cx, cz});
    }
  }
  std::sort(Gen.GenChunkQueue.begin(), Gen.GenChunkQueue.end(),
            [this](const GenChunkEntry &a, const GenChunkEntry &b)
            {
              const int ax = a.Cx * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int az = a.Cz * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int bx = b.Cx * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int bz = b.Cz * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int da = (ax - Gen.GenCenterX) * (ax - Gen.GenCenterX) +
                             (az - Gen.GenCenterZ) * (az - Gen.GenCenterZ);
              const int db = (bx - Gen.GenCenterX) * (bx - Gen.GenCenterX) +
                             (bz - Gen.GenCenterZ) * (bz - Gen.GenCenterZ);
              return da < db;
            });
  if (world.GetProceduralSettings().AsyncChunkGeneration)
  {
    // Parallel generation commits complete chunks, so its progress numerator
    // advances by CHUNK_SIZE squared per result rather than one column entry.
    Gen.GenTotalColumns = static_cast<int>(Gen.GenChunkQueue.size() *
                                       CHUNK_SIZE * CHUNK_SIZE);
  }
}

void UWorldCooperativeSession::EnsureParallelGenerationInfrastructure(
    UWorld &world)
{
  if (!ParallelGen)
  {
    ParallelGen = new CooperativeParallelGenState();
  }
  if (!ParallelGen->Populator || ParallelGen->PopulatorObjectLibrary != world.ObjectLibrary)
  {
    ParallelGen->Populator = std::make_unique<UPipelineChunkPopulator>(
        *world.BlockRegistry, world.ObjectLibrary, world.WorldgenOwnerPackId);
    ParallelGen->PopulatorObjectLibrary = world.ObjectLibrary;
  }
}

bool UWorldCooperativeSession::AdvanceParallelGeneration(UWorld &world,
                                                         int budget)
{
  EnsureParallelGenerationInfrastructure(world);
  if (!ParallelGen || !ParallelGen->Populator)
  {
    return AdvanceGeneration(world, budget);
  }

  world.SetCooperativeBulkGenerating(true);
  const ProceduralSettings settings = world.GetProceduralSettings();
  const std::size_t max_inflight =
      ComputeWorkerThreadCount(JobPoolKind::CoopGeneration) * 2;
  UChunkGenerationRegistry *tokens =
      world.Streaming ? &world.Streaming->GetChunkGenTokens() : nullptr;

  for (ChunkPopulateResult &result :
       ParallelGen->Completed.DrainUpTo(static_cast<std::size_t>(budget) * 2))
  {
    if (result.discarded)
    {
      if (ParallelGen->InFlight > 0)
      {
        --ParallelGen->InFlight;
      }
      continue;
    }
    if (tokens)
    {
      const uint64_t current = tokens->Current(result.coord).sequence;
      if (!result.token.IsValidFor(result.coord, current))
      {
        if (ParallelGen->InFlight > 0)
        {
          --ParallelGen->InFlight;
        }
        continue;
      }
    }
    result.buffer.ApplyTo(world.BlockWorld);
    Gen.GenDoneColumns += CHUNK_SIZE * CHUNK_SIZE;
    if (ParallelGen->InFlight > 0)
    {
      --ParallelGen->InFlight;
    }
  }

  int scheduled = 0;
  while (Gen.GenChunkScheduleIndex < Gen.GenChunkQueue.size() &&
         ParallelGen->InFlight < max_inflight &&
         scheduled < budget * 2)
  {
    const GenChunkEntry &entry = Gen.GenChunkQueue[Gen.GenChunkScheduleIndex++];
    ChunkPopulateRequest request;
    request.chunkCoord = glm::ivec3(entry.Cx, 0, entry.Cz);
    request.settings = settings;
    if (tokens)
    {
      request.token = tokens->Current(request.chunkCoord);
      const uint64_t start_sequence = request.token.sequence;
      const glm::ivec3 coord = request.chunkCoord;
      request.shouldCancel = [tokens, coord, start_sequence]()
      { return tokens->Current(coord).sequence != start_sequence; };
    }
    else
    {
      request.token.coord = request.chunkCoord;
      request.token.sequence = 1;
    }
    request.columnOrigin = glm::ivec2(Gen.GenCenterX, Gen.GenCenterZ);
    request.hasColumnOrigin = true;
    request.objects = world.ObjectLibrary;
    request.content = CaptureWorldGenContentSnapshot();
    ++ParallelGen->InFlight;
    ++scheduled;
    UPipelineChunkPopulator *populator = ParallelGen->Populator.get();
    UCompletedJobQueue<ChunkPopulateResult> *completed = &ParallelGen->Completed;
    ParallelGen->Pool.Enqueue(
        [populator, request, completed]()
        {
          ChunkPopulateResult result = populator->Populate(request);
          completed->Push(std::move(result));
        });
  }

  const bool queue_empty =
      Gen.GenChunkScheduleIndex >= Gen.GenChunkQueue.size() && ParallelGen->InFlight == 0 &&
      ParallelGen->Completed.Empty();
  if (queue_empty)
  {
    // UX U2/S3: workers may still be running their last jobs; drain over
    // frames (short non-blocking wait per tick) instead of blocking the
    // main thread up to 2 seconds.
    if (ParallelGen->Pool.GetActiveJobCount() > 0)
    {
      (void)ParallelGen->Pool.WaitIdleFor(std::chrono::milliseconds(2));
      return false; // keep the generation phase alive one more frame
    }
    if (ParallelGen->Pool.GetActiveJobCount() == 0 &&
        ParallelGen->Pool.GetPendingJobCount() > 0)
    {
      std::cerr << "CoopGeneration: pending jobs with idle workers"
                << std::endl;
      ParallelGen->Pool.CancelPendingJobs();
      (void)ParallelGen->Completed.DrainAll();
      ParallelGen->InFlight = 0;
    }
    world.SetCooperativeBulkGenerating(false);
    return true;
  }
  return false;
}

bool UWorldCooperativeSession::LoadOneChunkFile(
    UWorld &world, const std::filesystem::path &path)
{
  const std::string stem = path.stem().string();
  const size_t u1 = stem.find('_');
  const size_t u2 = stem.find('_', u1 + 1);
  if (u1 == std::string::npos || u2 == std::string::npos)
  {
    return false;
  }
  try
  {
    const int cx = std::stoi(stem.substr(0, u1));
    const int cy = std::stoi(stem.substr(u1 + 1, u2 - u1 - 1));
    const int cz = std::stoi(stem.substr(u2 + 1));
    if (!world.BlockRegistry)
    {
      return false;
    }
    const int placed = world.GetChunkStorage().LoadChunk(
        glm::ivec3(cx, cy, cz), world.BlockWorld, FolderPath,
        *world.BlockRegistry);
    if (placed >= 0)
    {
      ++ChunkFilesRead;
      VoxelsFromChunkFiles += static_cast<size_t>(placed);
      return true;
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "Skipping chunk file " << path.string() << ": " << e.what()
              << std::endl;
  }
  return false;
}

bool UWorldCooperativeSession::AdvanceGeneration(UWorld &world, int budget)
{
  if (!world.WorldGen)
  {
    return true;
  }
  // UX U2/S2: the sync fallback previously trusted a column-count budget;
  // a heavy column (caves, water) made a single frame unbounded. Wall cap.
  const auto tick_start = std::chrono::steady_clock::now();
  constexpr double kMaxWallMs = 40.0;
  int processed = 0;
  while (processed < budget && Gen.GenColumnIndex < Gen.GenColumnQueue.size())
  {
    const GenColumnEntry &entry = Gen.GenColumnQueue[Gen.GenColumnIndex++];
    world.WorldGen->GenerateColumn(entry.X, entry.Z);
    ++Gen.GenDoneColumns;
    ++processed;
    const double wall_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tick_start)
            .count();
    if (wall_ms >= kMaxWallMs)
    {
      break;
    }
  }
  return Gen.GenColumnIndex >= Gen.GenColumnQueue.size();
}

bool UWorldCooperativeSession::SealFluidInGenerationPatchRow(UWorld &world)
{
  const ProceduralSettings &settings = world.GetProceduralSettings();
  if (!settings.FillWater || !world.BlockRegistry)
  {
    return true;
  }
  const int min_cx = FloorDiv(Gen.GenPatchMinX, CHUNK_SIZE);
  const int max_cx = FloorDiv(Gen.GenPatchMaxX, CHUNK_SIZE);
  const int min_cz = FloorDiv(Gen.GenPatchMinZ, CHUNK_SIZE);
  const int max_cz = FloorDiv(Gen.GenPatchMaxZ, CHUNK_SIZE);
  if (Gen.FluidSealNextCx == INT_MIN)
  {
    Gen.FluidSealNextCx = min_cx;
  }
  if (Gen.FluidSealNextCx > max_cx)
  {
    Gen.FluidSealNextCx = INT_MIN;
    return true;
  }
  for (int cz = min_cz; cz <= max_cz; ++cz)
  {
    (void)SealFluidShoreOnChunkCommitted(
        world.BlockWorld, *world.BlockRegistry, settings,
        world.WorldgenOwnerPackId,
        glm::ivec3(Gen.FluidSealNextCx, 0, cz));
  }
  ++Gen.FluidSealNextCx;
  return Gen.FluidSealNextCx > max_cx;
}

void UWorldCooperativeSession::SealFluidInGenerationPatch(UWorld &world)
{
  // UX U2/S5: kept for synchronous callers (headless); the coop create path
  // uses SealFluidInGenerationPatchRow across frames.
  Gen.FluidSealNextCx = INT_MIN;
  while (!SealFluidInGenerationPatchRow(world))
  {
  }
}

bool UWorldCooperativeSession::Tick(UWorld &world, IUProgressSink &sink,
                                    int chunkBudget)
{
  if (!Active || Failed)
  {
    return true;
  }

  const int budget = std::max(1, chunkBudget);

  switch (CurrentPhase)
  {
  case Phase::Init:
  {
    HandlePhaseInit(world, sink, budget);
    break;
  }
  case Phase::Metadata:
  {
    world.LoadWorldData(FolderPath + "/world_data.json");
    ProceduralSettings settings = world.GetProceduralSettings();
    settings.AsyncChunkGeneration = RuntimeAsyncChunkGeneration;
    settings.AsyncChunkIo = RuntimeAsyncChunkIo;
    settings.MaxChunkCommitsPerFrame = RuntimeMaxChunkCommitsPerFrame;
    world.SetProceduralSettings(settings, /*rebuildPipeline=*/false);
    if (world.OnAfterWorldDataLoaded)
    {
      world.OnAfterWorldDataLoaded();
    }
    CurrentPhase = Phase::Entities;
    Report(sink, "metadata", kPhaseWeightMetadata, "Loading world data...");
    break;
  }
  case Phase::Entities:
  {
    world.ResetCreaturesBeforeEntityLoad();
    world.LoadUsers(FolderPath + "/users.json");
    world.LoadCreatures(FolderPath + "/creatures.json");
    world.LinkUsersToPlayerCreatures();
    world.RefreshBlockRegistry();
    world.GetChunkStorage().ApplyStorageMarkerFromDisk(FolderPath);
    Spatial.SpatialStreamingLoad = world.IsStreamingEnabled() && world.HasPersistedSave;
    Spatial.SpatialRadius = world.RenderDistanceChunks + 1;
    Spatial.SpatialCenter = ResolveSpatialLoadCenter(world);
    CurrentPhase = Phase::ScanChunks;
    Report(sink, "entities", -1.f, "Loading entities...");
    break;
  }
  case Phase::ScanChunks:
  {
    // UX U2/S8: one directory batch per tick under a wall budget; a large
    // worlds dir no longer sweeps in a single tick.
    if (!ScanChunksActive)
    {
      ChunkFiles.clear();
      ChunkFileIndex = 0;
      ScanChunksBatch.clear();
      const std::string chunks_dir = FolderPath + "/chunks";
      if (!Spatial.SpatialStreamingLoad && std::filesystem::exists(chunks_dir))
      {
        ScanChunksDir = chunks_dir;
        ScanChunksActive = true;
      }
      else
      {
        CurrentPhase = Phase::LoadChunks;
        Report(sink, "scan", -1.f, "Scanning chunk files...");
        break;
      }
    }
    if (ScanChunkFilesBatch(world, 8.0))
    {
      ScanChunksActive = false;
      ChunkFiles = std::move(ScanChunksBatch);
      ScanChunksBatch.clear();
      std::sort(ChunkFiles.begin(), ChunkFiles.end());
      CurrentPhase = Phase::LoadChunks;
    }
    Report(sink, "scan", -1.f, "Scanning chunk files...");
    break;
  }
  case Phase::LoadChunks:
  {
    HandlePhaseLoadChunks(world, sink, budget);
    break;
  }
  case Phase::SpatialChunks:
  {
    HandlePhaseSpatialChunks(world, sink, budget);
    break;
  }
  case Phase::RelightChunks:
  {
    HandlePhaseRelightChunks(world, sink, budget);
    break;
  }
  case Phase::RelightBulkChunks:
  {
    HandlePhaseRelightBulkChunks(world, sink, budget);
    break;
  }
  case Phase::RelightColumns:
  {
    HandlePhaseRelightColumns(world, sink, budget);
    break;
  }
  case Phase::RelightEmissiveBlockLight:
  {
    HandlePhaseRelightEmissiveBlockLight(world, sink, budget);
    break;
  }
  case Phase::MeshWarmup:
  {
    HandlePhaseMeshWarmup(world, sink, budget);
    break;
  }
  case Phase::PostLoadAnalysis:
  {
    HandlePhasePostLoadAnalysis(world, sink, budget);
    break;
  }
  case Phase::ProceduralFill:
  {
    HandlePhaseProceduralFill(world, sink, budget);
    break;
  }
  case Phase::ResolveSpawn:
  {
    HandlePhaseResolveSpawn(world, sink, budget);
    break;
  }
  case Phase::FinalizeWorld:
  {
    world.InitStreamerCallbacks();
    if (world.Streaming->HasStreamer() && world.LoadedFromChunkSave)
    {
      world.Streaming->MarkPersistedColumnsFromWorld();
    }
    if (world.MeshService->HasPendingDirty() ||
        world.MeshService->HasPendingAsyncMeshWork())
    {
      MeshWarmup.MeshWarmupFinalizeOnly = true;
      BeginMeshWarmup(world);
      Report(sink, "mesh_warmup",
             ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                    : CooperativeLoadProgressBase(),
             "Building terrain meshes...");
      break;
    }
    BeginPrepareEnter();
    break;
  }
  case Phase::PrepareEnter:
  {
    world.FinalizePlayerAfterWorldLoad();
    if (auto user = world.GetCurrentUser())
    {
      world.ApplyUserToCamera(user);
    }
    else
    {
      world.ApplySpawnToCamera();
    }
    CurrentPhase = Phase::PrepareView;
    const float prepare_base =
        Kind == WorldCoopKind::Create
            ? (CooperativeCreateMeshProgressBase() + kCreateWeightMeshWarmup)
            : (ProceduralFillLoadPath
                   ? (ProceduralFillMeshProgressBase() +
                      kProceduralFillWeightMeshWarmup)
                   : CooperativeLoadProgressAfterMesh());
    Report(sink, "prepare_enter", prepare_base, "Placing player...");
    break;
  }
  case Phase::PrepareView:
  {
    HandlePhasePrepareView(world, sink, budget);
    break;
  }
  case Phase::DrainAsyncIo:
  {
    HandlePhaseDrainAsyncIo(world, sink, budget);
    break;
  }
  case Phase::ScanSaveChunks:
  {
    CubatariumLogInfo("Save", "scan_save_chunks begin");
    ScanSaveChunkCoords(world);
    CurrentPhase = Phase::SaveChunks;
    Report(sink, "scan", 0.02f, "Collecting chunks...");
    CubatariumLogInfo(
        "Save",
        std::string("scan_save_chunks end count=") +
            std::to_string(Save.SaveChunkCoords.size()));
    break;
  }
  case Phase::SaveChunks:
  {
    HandlePhaseSaveChunks(world, sink, budget);
    break;
  }
  case Phase::SaveMetadataMarker:
  {
    // UX U2/S7: the old single SaveMetadata tick wrote five files in one
    // go (10-100s ms+ with inventories) - one file per tick now, each with
    // an indeterminate marquee report.
    CubatariumLogInfo("Save", "save_metadata begin");
    world.GetChunkStorage().WriteStorageMarker(FolderPath);
    CurrentPhase = Phase::SaveMetadataUsers;
    Report(sink, "save_metadata", -1.f, "Saving world metadata...");
    break;
  }
  case Phase::SaveMetadataUsers:
  {
    world.SaveUsers(FolderPath + "/users.json");
    CurrentPhase = Phase::SaveMetadataCreatures;
    Report(sink, "save_metadata", -1.f, "Saving world metadata...");
    break;
  }
  case Phase::SaveMetadataCreatures:
  {
    world.SaveCreatures(FolderPath + "/creatures.json");
    CurrentPhase = Phase::SaveMetadataWorldData;
    Report(sink, "save_metadata", -1.f, "Saving world metadata...");
    break;
  }
  case Phase::SaveMetadataWorldData:
  {
    world.SaveWorldData(FolderPath + "/world_data.json");
    CurrentPhase = Phase::SaveMetadataDiag;
    Report(sink, "save_metadata", -1.f, "Saving world metadata...");
    break;
  }
  case Phase::SaveMetadataDiag:
  {
    world.SaveMovementDiagnostics(FolderPath + "/movement_diagnostics.json");
    world.ModifiedChunks.clear();
    if (ResumeStreamingAfterSave)
    {
      CubatariumLogInfo("Save", "save_metadata resume_streaming");
      world.EnsureStreamingActiveAfterBackgroundQuiesce();
    }
    CurrentPhase = Phase::Done;
    Active = false;
    CubatariumLogInfo("Save", "save_metadata end");
    Report(sink, "done", 1.f, "World saved.");
    break;
  }
  case Phase::GenerateColumns:
  {
    HandlePhaseGenerateColumns(world, sink, budget);
    break;
  }
  case Phase::PostCreate:
  {
    HandlePhasePostCreate(world, sink, budget);
    break;
  }
  case Phase::Done:
    Active = false;
    return true;
  }

  if (CurrentPhase != LastDiagPhase)
  {
    LogWorldLoadDiag(PhaseId(), world);
    LastDiagPhase = CurrentPhase;
  }

  RecordPhaseTiming(world);

  return CurrentPhase == Phase::Done;
}


void UWorldCooperativeSession::HandlePhaseInit(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    if (Kind == WorldCoopKind::Load)
    {
      world.SetWorldFolderPath(FolderPath);
      world.ClearSpawnAreaPreparedByCooperativeLoad();
      world.SetLightingRelightDeferred(true);
      world.SetLightingSkylightBulkComplete(false);
      world.BlockWorldReady = false;
      world.LoadedFromChunkSave = false;
      world.MeshService->CancelAsyncMeshWork();
      world.BlockWorld.Clear();
      world.MeshService->GetCache().MarkAllDirty();
      UChunkRenderDemandStore::Get().Clear();
      ResetFluidSurfacePackReuseCache();
      world.ResetPhysicsRuntimeState();
      world.CancelAsyncRelightWork();
      world.ModifiedChunks.clear();
      world.MovementDiagHistory.clear();
      ChunksFileName = FolderPath + "/chunks.json";
      world.HasPersistedSave = UWorld::HasPersistedTerrainOnDisk(FolderPath);
      world.AllowProceduralFill = !world.HasPersistedSave;
      CurrentPhase = Phase::Metadata;
      Report(sink, "init", 0.f, "Preparing world...");
    }
    else if (Kind == WorldCoopKind::Save)
    {
      // Never QuiesceBackgroundWork here: it latches BackgroundQuiesceFinished
      // and disables the streamer mid-autosave. Quit already budget-quiesced.
      if (world.Streaming)
      {
        world.Streaming->CancelChunkGeneration();
      }
      if (world.Persistence)
      {
        // Cancel pending IO only — do not WaitIdle (can stick on late disk IO).
        (void)world.Persistence->AbortAsyncChunkIoFor(
            std::chrono::milliseconds(0));
      }
      // Quit-save: skip catalog reload (unchanged after quiesce).
      if (ResumeStreamingAfterSave)
      {
        world.RefreshBlockRegistry();
      }
      std::filesystem::create_directories(FolderPath);
      world.SetWorldFolderPath(FolderPath);
      std::filesystem::create_directories(FolderPath + "/chunks");
      Save.SaveDrainIoFrames = 0;
      // Quit path: skip DrainAsyncIo and run scan inline so we never hang
      // between "skip → scan" and the next Tick (log stopped after skip).
      if (!ResumeStreamingAfterSave)
      {
        CubatariumLogInfo("Save", "quit-save: inline scan after quiesce");
        // Cancel + abandon workers before WaitIdle — populate/seal must not
        // block Save Init (hang after "World saved.").
        if (world.Streaming)
        {
          world.Streaming->CancelChunkGeneration();
          CubatariumLogInfo("Save", "quit-save: abandon");
          world.Streaming->AbandonWorkersForProcessExit(
              std::chrono::milliseconds(150));
        }
        CubatariumLogInfo("Save", "quit-save: scanning chunks");
        ScanSaveChunkCoords(world);
        CurrentPhase = Phase::SaveChunks;
        Report(sink, "scan", 0.02f, "Collecting chunks...");
        CubatariumLogInfo(
            "Save",
            std::string("scan_save_chunks end count=") +
                std::to_string(Save.SaveChunkCoords.size()));
      }
      else
      {
        CurrentPhase = Phase::DrainAsyncIo;
        Report(sink, "init", 0.f, "Preparing save...");
      }
    }
    else
    {
      world.ClearCreaturesAndUsers();
      world.SetLightingRelightDeferred(true);
      world.BlockWorldReady = false;
      world.HasPersistedSave = false;
      world.LoadedFromChunkSave = false;
      world.AllowProceduralFill = true;
      world.RefreshBlockRegistry();
      if (world.ObjectLibrary && world.BlockRegistry)
      {
        world.ObjectLibrary->RebindBlockIds(*world.BlockRegistry);
      }
      world.WorldGenSetsData = BuildDefaultWorldGenSets();
      world.RebuildResolvedObjectFeatures();
      world.RebuildWorldGenPipeline();
      world.BlockWorld.Clear();
      world.CancelAsyncRelightWork();
      world.ModifiedChunks.clear();
      FolderPath = world.GetWorldFolderPath();
      if (!FolderPath.empty())
      {
        std::filesystem::create_directories(FolderPath);
        std::filesystem::create_directories(FolderPath + "/chunks");
        world.SetWorldFolderPath(FolderPath);
      }
      CurrentPhase = Phase::GenerateColumns;
      InitGenerationGrid(world);
      Report(sink, "init", 0.f, "Generating terrain...");
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseLoadChunks(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    int loaded = 0;
    while (ChunkFileIndex < ChunkFiles.size() && loaded < budget)
    {
      LoadOneChunkFile(world, ChunkFiles[ChunkFileIndex++]);
      ++loaded;
    }
    {
      const float chunkBase = kPhaseWeightMetadata + kPhaseWeightEntities;
      const float chunkSpan = kPhaseWeightChunks;
      const float frac =
          ChunkFiles.empty()
              ? chunkBase + chunkSpan
              : chunkBase + chunkSpan * (static_cast<float>(ChunkFileIndex) /
                                         static_cast<float>(ChunkFiles.size()));
      Report(sink, "chunks", frac,
             "Loading chunks (" + std::to_string(ChunkFileIndex) + "/" +
                 std::to_string(ChunkFiles.size()) + ")...",
             static_cast<int>(ChunkFileIndex),
             static_cast<int>(ChunkFiles.size()));
    }
    if (ChunkFileIndex >= ChunkFiles.size())
    {
      // UX U2/S6: early-out existence check instead of a full-world scan
      // in one tick (the codebase itself warned these "look like a hang").
      const bool blocksInWorld = world.BlockWorld.HasAnyNonAir();
      world.LoadedFromChunkSave = world.HasPersistedSave || ChunkFilesRead > 0 ||
                                  VoxelsFromChunkFiles > 0 || blocksInWorld;
      world.AllowProceduralFill = world.IsStreamingEnabled();
      if (Spatial.SpatialStreamingLoad && world.LoadedFromChunkSave)
      {
        Spatial.SpatialColumnQueue.clear();
        Spatial.SpatialAsyncColumns.clear();
        Spatial.SpatialColumnQueueIndex = 0;
        Spatial.SpatialColumnsVisited = 0;
        for (int ring = 0; ring <= Spatial.SpatialRadius; ++ring)
        {
          for (int dx = -ring; dx <= ring; ++dx)
          {
            for (int dz = -ring; dz <= ring; ++dz)
            {
              if (std::max(std::abs(dx), std::abs(dz)) != ring)
              {
                continue;
              }
              Spatial.SpatialColumnQueue.emplace_back(Spatial.SpatialCenter.x + dx, 0,
                                              Spatial.SpatialCenter.z + dz);
            }
          }
        }
        CurrentPhase = Phase::SpatialChunks;
        Report(sink, "spatial", kPhaseWeightMetadata + kPhaseWeightEntities +
                                    kPhaseWeightChunks,
               "Loading nearby terrain...");
      }
      else
      {
        BeginMeshWarmup(world);
      }
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseSpatialChunks(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const int total = (2 * Spatial.SpatialRadius + 1) * (2 * Spatial.SpatialRadius + 1);
    int queued = 0;
    while (Spatial.SpatialColumnQueueIndex < Spatial.SpatialColumnQueue.size() &&
           queued < budget)
    {
      const glm::ivec3 ground = Spatial.SpatialColumnQueue[Spatial.SpatialColumnQueueIndex++];
      const bool async_spatial_load =
          world.Persistence && world.ProceduralTemplate.AsyncChunkIo;
      if (async_spatial_load)
      {
        world.Persistence->RequestAsyncTerrainColumnLoad(world, ground);
        if (world.Persistence->IsTerrainColumnDiskLoadPending(ground))
        {
          Spatial.SpatialAsyncColumns.push_back(ground);
        }
      }
      else if (world.BlockRegistry)
      {
        const auto column_load_started = std::chrono::steady_clock::now();
        const int loaded_slices = world.GetChunkStorage().LoadTerrainColumn(
            ground, world.BlockWorld, FolderPath, *world.BlockRegistry,
            world.ProceduralTemplate.MaxHeight,
            world.ProceduralTemplate.MinHeight);
        bool purged_incomplete_column = false;
        // LoadTerrainColumn returns -1 when a save still owns this column.
        // That is a deferred read, not an absent or torn disk column. Keep the
        // in-memory/save owner intact instead of clearing its chunks and files.
        if (loaded_slices >= 0 &&
            !IsTerrainChunkComplete(world.BlockWorld, ground,
                                    world.ProceduralTemplate.MaxHeight,
                                    kNoTerrainChunkSlice,
                                    world.ProceduralTemplate.MinHeight))
        {
          // Stale/torn column: drop disk + RAM so streaming regenerates.
          ClearTerrainColumnChunks(world.BlockWorld, ground,
                                   world.ProceduralTemplate.MaxHeight,
                                   world.ProceduralTemplate.MinHeight);
          world.GetChunkStorage().RemoveTerrainColumnFromDisk(
              FolderPath, ground, world.ProceduralTemplate.MaxHeight, nullptr,
              world.ProceduralTemplate.MinHeight);
          purged_incomplete_column = loaded_slices > 0;
        }
        if (IsWorldColumnSourceTraceEnabled())
        {
          const double column_load_ms =
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - column_load_started)
                  .count();
          const std::string source =
              loaded_slices == 0 ? "disk_miss" : "disk";
          std::string outcome = "spatial_load";
          if (loaded_slices < 0)
          {
            outcome = "deferred_save_pending";
          }
          else if (loaded_slices == 0)
          {
            outcome = "absent";
          }
          else if (purged_incomplete_column)
          {
            outcome = "purged_incomplete";
          }
          const std::string message =
              "source=" + source + " outcome=" + outcome + " coord=(" +
              std::to_string(ground.x) + ",0," + std::to_string(ground.z) +
              ") slices=" + std::to_string(loaded_slices) + " elapsed_ms=" +
              std::to_string(column_load_ms);
          CubatariumLogInfo("WorldColumnSource", message);
        }
      }
      ++Spatial.SpatialColumnsVisited;
      ++queued;
    }
    if (world.Persistence && world.ProceduralTemplate.AsyncChunkIo)
    {
      // The entry gate used to read/decode 121 complete terrain columns
      // synchronously. Reuse the streaming disk worker, but keep the loading
      // screen responsive and bound main-thread deserialization/application.
      world.Persistence->TickAsyncChunkIo(world, 10, 6.0);
    }
    size_t pending_columns = 0;
    if (world.Persistence)
    {
      for (const glm::ivec3 &coord : Spatial.SpatialAsyncColumns)
      {
        if (world.Persistence->IsTerrainColumnDiskLoadPending(coord))
        {
          ++pending_columns;
        }
      }
    }
    const size_t completed_columns =
        Spatial.SpatialColumnsVisited >= pending_columns
            ? Spatial.SpatialColumnsVisited - pending_columns
            : 0;
    const float chunkBase =
        kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks;
    const float frac =
        chunkBase + kPhaseWeightSpatial *
                        (static_cast<float>(completed_columns) /
                         static_cast<float>(std::max(1, total)));
    Report(sink, "spatial", frac,
           "Loading nearby terrain (" + std::to_string(completed_columns) +
               "/" + std::to_string(total) + ")...",
           completed_columns, total);
    if (Spatial.SpatialColumnQueueIndex >= Spatial.SpatialColumnQueue.size() &&
        pending_columns == 0)
    {
      BeginMeshWarmup(world);
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseRelightChunks(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    if (world.BlockRegistry)
    {
      const int relight_budget = std::max(budget * 4, 16);
      int relit = 0;
      while (Relight.RelightQueueIndex < Relight.RelightQueue.size() &&
             relit < relight_budget)
      {
        const glm::ivec3 coord = Relight.RelightQueue[Relight.RelightQueueIndex++];
        world.GetLightingPipeline().RelightChunk(
            world.BlockWorld, *world.BlockRegistry, coord, false, true);
        world.NoteChunkSliceLightCalculationSettled(coord);
        ++relit;
      }
    }
    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const float relight_frac = ComputeRelightLoadProgress(
        relight_base, relight_weight, Relight.RelightQueueIndex, Relight.RelightQueue.size(), 0,
        0, 0);
    Report(sink, "relight", relight_frac, "Computing lighting...");
    if (Relight.RelightQueueIndex >= Relight.RelightQueue.size())
    {
      BeginColumnRelightQueue(world);
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseRelightBulkChunks(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const auto tick_t0 = std::chrono::steady_clock::now();
    if (world.BlockRegistry)
    {
      const int chunk_budget = std::clamp(budget * 4, 16, 64);
      int relit = 0;
      while (Relight.BulkRelightChunkScheduledIndex < Relight.BulkRelightChunkQueue.size() &&
             relit < chunk_budget)
      {
        const glm::ivec3 coord =
            Relight.BulkRelightChunkQueue[Relight.BulkRelightChunkScheduledIndex++];
        world.GetLightingPipeline().RelightChunk(
            world.BlockWorld, *world.BlockRegistry, coord, false, true);
        world.NoteChunkSliceLightCalculationSettled(coord);
        ++relit;
      }
    }

    const bool chunks_done =
        Relight.BulkRelightChunkScheduledIndex >= Relight.BulkRelightChunkQueue.size();

    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const size_t relight_done = Relight.BulkRelightChunkScheduledIndex;
    const size_t relight_total = Relight.BulkRelightChunkQueue.size();
    const float relight_inner =
        relight_total > 0
            ? std::min(1.0f, static_cast<float>(relight_done) /
                                 static_cast<float>(relight_total))
            : 1.0f;
    const float relight_frac = relight_base + relight_weight * relight_inner;

    if (chunks_done)
    {
      const auto tick_t1 = std::chrono::steady_clock::now();
      const double tick_ms =
          std::chrono::duration<double, std::milli>(tick_t1 - tick_t0).count();
      std::cout << "[WorldLoad] RelightBulkChunks done: " << relight_total
                << " chunks, last_tick_ms=" << tick_ms << std::endl;
      BeginEmissiveBlockLightQueue(world);
      if (CurrentPhase == Phase::MeshWarmup)
      {
        ReportMeshWarmupStart(sink);
      }
    }
    else
    {
      Report(sink, "relight", relight_frac,
             "Computing lighting... " + std::to_string(relight_done) + "/" +
                 std::to_string(relight_total),
             relight_done, relight_total);
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseRelightColumns(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const int min_y = world.ProceduralTemplate.MinHeight;
    const int max_y = world.ProceduralTemplate.MaxHeight;
    const auto tick_t0 = std::chrono::steady_clock::now();
    const bool use_async = world.ProceduralTemplate.AsyncRelight &&
                           world.AllowsAsyncLighting() && world.BlockRegistry;
    if (use_async)
    {
      // Era26 I-L1: scoped async RelightColumns (snapshot JobPool). Drain
      // without frontier re-queue / priority mesh — streaming is blocked.
      world.DrainAsyncRelightResults(/*max_per_frame=*/48,
                                     /*priority_mesh=*/false,
                                     /*enqueue_background_frontier=*/false);
      world.ReconcileAsyncRelightColumnInFlight();

      const int workers =
          std::clamp(world.ProceduralTemplate.RelightThreadCount, 1, 8);
      const int max_inflight = workers * 3;
      int schedule_batch = std::clamp(budget, 2, 8);
      while (Relight.ColumnRelightScheduledIndex < Relight.ColumnRelightQueue.size() &&
             schedule_batch > 0 &&
             world.GetAsyncRelightInFlightCount() < max_inflight)
      {
        const glm::ivec2 &col =
            Relight.ColumnRelightQueue[Relight.ColumnRelightScheduledIndex++];
        world.EnqueueAsyncTerrainColumnRelight(
            col.x * CHUNK_SIZE, col.y * CHUNK_SIZE, min_y, max_y,
            /*include_skylight=*/true, /*include_block_light=*/false,
            /*finalize_pending_gate=*/false,
            /*visible_draw_gate_repair=*/false, "world_load");
        --schedule_batch;
      }
      Relight.ColumnRelightIndex = Relight.ColumnRelightScheduledIndex;
    }
    else if (world.BlockRegistry)
    {
      const int column_budget = std::clamp(budget, 8, 32);
      int relit = 0;
      while (Relight.ColumnRelightIndex < Relight.ColumnRelightQueue.size() &&
             relit < column_budget)
      {
        const glm::ivec2 &col = Relight.ColumnRelightQueue[Relight.ColumnRelightIndex++];
        world.GetLightingPipeline().RelightColumn(
            world.BlockWorld, *world.BlockRegistry, col.x, col.y, min_y, max_y,
            false, true);
        NoteColumnLightCalculationSettled(world, col, min_y, max_y);
        ++relit;
      }
      Relight.ColumnRelightScheduledIndex = Relight.ColumnRelightIndex;
    }

    const bool columns_scheduled =
        Relight.ColumnRelightScheduledIndex >= Relight.ColumnRelightQueue.size();
    const bool async_idle =
        !use_async || !world.HasPendingAsyncRelightWork();
    const bool columns_done = columns_scheduled && async_idle;
    const size_t relight_done = Relight.ColumnRelightScheduledIndex;
    const size_t relight_total = Relight.ColumnRelightQueue.size();

    bool force_done = columns_done;
    if (!force_done && Relight.RelightColumnsStartedAt.time_since_epoch().count() != 0)
    {
      const auto wall_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - Relight.RelightColumnsStartedAt)
              .count();
      if (wall_ms >= kRelightColumnsMaxWallMs)
      {
        std::cerr << "RelightColumns: wall timeout after " << wall_ms
                  << "ms, done=" << relight_done << "/" << relight_total
                  << " — continuing to mesh warmup" << std::endl;
        Relight.ColumnRelightIndex = Relight.ColumnRelightQueue.size();
        Relight.ColumnRelightScheduledIndex = Relight.ColumnRelightQueue.size();
        if (use_async)
        {
          world.CancelAsyncRelightWork();
        }
        force_done = true;
      }
    }

    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const float relight_inner =
        relight_total > 0
            ? std::min(1.0f, static_cast<float>(Relight.ColumnRelightScheduledIndex) /
                                 static_cast<float>(relight_total))
            : 1.0f;
    const float relight_frac = relight_base + relight_weight * relight_inner;
    Report(sink, "relight", relight_frac,
           force_done ? "Lighting ready."
                      : "Computing lighting... " +
                            std::to_string(Relight.ColumnRelightScheduledIndex) + "/" +
                            std::to_string(relight_total));

    if (force_done)
    {
      if (use_async && world.HasPendingAsyncRelightWork())
      {
        world.CancelAsyncRelightWork();
      }
      const auto tick_t1 = std::chrono::steady_clock::now();
      const double tick_ms =
          std::chrono::duration<double, std::milli>(tick_t1 - tick_t0).count();
      std::cout << "[WorldLoad] RelightColumns done: " << relight_total
                << " columns, async=" << (use_async ? 1 : 0)
                << ", last_tick_ms=" << tick_ms << std::endl;
      BeginEmissiveBlockLightQueue(world);
      if (CurrentPhase == Phase::MeshWarmup)
      {
        ReportMeshWarmupStart(sink);
      }
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseRelightEmissiveBlockLight(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    // UX U2/S4: drain the pending 16^3 emissive scans first, a few chunks
    // per tick under a wall budget (the scan burst used to run inside
    // BeginEmissiveBlockLightQueue in one tick).
    if (!Relight.EmissiveChunkScanQueue.empty() && world.BlockRegistry)
    {
      const auto scan_start = std::chrono::steady_clock::now();
      constexpr double kScanMaxWallMs = 12.0;
      while (!Relight.EmissiveChunkScanQueue.empty())
      {
        const glm::ivec3 coord = Relight.EmissiveChunkScanQueue.back();
        Relight.EmissiveChunkScanQueue.pop_back();
        bool has_emissive = false;
        if (const UChunk *chunk =
                world.BlockWorld.GetChunkManager().GetChunk(coord))
        {
          for (int ly = 0; ly < CHUNK_SIZE && !has_emissive; ++ly)
          {
            for (int lz = 0; lz < CHUNK_SIZE && !has_emissive; ++lz)
            {
              for (int lx = 0; lx < CHUNK_SIZE; ++lx)
              {
                const BlockId id =
                    chunk->GetBlockLocal(glm::ivec3(lx, ly, lz));
                if (world.BlockRegistry->GetLightEmission(id) > 0)
                {
                  has_emissive = true;
                  break;
                }
              }
            }
          }
        }
        if (has_emissive)
        {
          Relight.EmissiveChunkRelightQueue.push_back(coord);
        }
        const double scan_wall_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - scan_start)
                .count();
        if (scan_wall_ms >= kScanMaxWallMs)
        {
          break;
        }
      }
      Report(sink, "relight", -1.f, "Computing block light...");
      return; // scans still pending; keep this phase one more frame
    }
    if (world.BlockRegistry)
    {
      const int relight_budget = std::max(budget * 4, 16);
      int relit = 0;
      while (Relight.EmissiveChunkRelightIndex < Relight.EmissiveChunkRelightQueue.size() &&
             relit < relight_budget)
      {
        const glm::ivec3 coord =
            Relight.EmissiveChunkRelightQueue[Relight.EmissiveChunkRelightIndex++];
        world.GetLightingPipeline().RelightChunkBlockLight(
            world.BlockWorld, *world.BlockRegistry, coord);
        world.NoteChunkSliceLightCalculationSettled(coord);
        ++relit;
      }
    }
    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const size_t emissive_done = Relight.EmissiveChunkRelightIndex;
    const size_t emissive_total = Relight.EmissiveChunkRelightQueue.size();
    const float relight_frac = ComputeRelightLoadProgress(
        relight_base, relight_weight, emissive_done, emissive_total, 0, 0, 0);
    Report(sink, "relight", relight_frac, "Computing block light...");

    if (Relight.EmissiveChunkRelightIndex >= Relight.EmissiveChunkRelightQueue.size())
    {
      FinishEmissiveBlockLightRelight(world);
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseMeshWarmup(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const bool create_mesh_warmup = Kind == WorldCoopKind::Create;
    const bool force_sync_mesh = create_mesh_warmup;
    const UChunkEmergeCoordinator::FrameBudget mesh_budget =
        create_mesh_warmup
            ? UChunkEmergeCoordinator::CreateMeshWarmupBudget(budget)
            : UChunkEmergeCoordinator::CooperativeWarmupBudget(budget);
    const int pass_limit = create_mesh_warmup ? 8 : 1;
    const int sync_mesh_budget =
        force_sync_mesh ? std::max(8, budget * 4) : mesh_budget.MaxMeshDrain;
    if (Kind == WorldCoopKind::Load)
    {
      ParkSpawnRingMeshWhileRelightDeferred(world);
      if (world.IsEnterSessionActive())
      {
        world.MarkEnterMissingMeshesDirty();
      }
    }
    MeshRebuildTickStats tick_stats;
    // UX U2/S11: the create path forces sync meshing (8 passes x up to
    // budget*4 chunks); a wall cap keeps each frame bounded on heavy chunks.
    const auto mesh_tick_start = std::chrono::steady_clock::now();
    constexpr double kMeshTickMaxWallMs = 100.0;
    for (int pass = 0; pass < pass_limit; ++pass)
    {
      if (!world.BlockRegistry)
      {
        break;
      }
      const double mesh_wall_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - mesh_tick_start)
              .count();
      if (pass > 0 && mesh_wall_ms >= kMeshTickMaxWallMs)
      {
        break;
      }
      const int drain_budget =
          force_sync_mesh ? sync_mesh_budget : mesh_budget.MaxMeshDrain;
      const int schedule_budget =
          force_sync_mesh ? sync_mesh_budget : mesh_budget.MaxMeshSchedule;
      MeshRebuildTickStats pass_stats =
          world.MeshService->RebuildDirtyChunksWithStats(
              world.BlockWorld, *world.BlockRegistry, drain_budget,
              schedule_budget, force_sync_mesh);
      tick_stats.Completed += pass_stats.Completed;
      tick_stats.Scheduled += pass_stats.Scheduled;
      tick_stats.SyncRebuilt += pass_stats.SyncRebuilt;
      if (!force_sync_mesh)
      {
        world.MeshService->DrainAsyncMeshResults(
            world.BlockWorld, *world.BlockRegistry, mesh_budget.MaxMeshDrain);
      }
      if (!world.MeshService->HasPendingDirty() &&
          !world.MeshService->HasPendingAsyncMeshWork())
      {
        break;
      }
    }
    if (MeshWarmup.MeshWarmupTicks == 0)
    {
      MeshWarmup.MeshWarmupStartedAt = std::chrono::steady_clock::now();
      std::cout << "[WorldLoad] MeshWarmup start: pending="
                << world.MeshService->GetDirtyCount() << " inflight="
                << world.MeshService->GetAsyncInFlightCount()
                << " sync=" << (force_sync_mesh ? "yes" : "no") << std::endl;
    }
    MeshWarmup.MeshWarmupCompletedTotal += static_cast<size_t>(tick_stats.Completed);
    ++MeshWarmup.MeshWarmupTicks;
    const bool mesh_done_raw = !world.MeshService->HasPendingDirty() &&
                               !world.MeshService->HasPendingAsyncMeshWork();
    bool mesh_done = mesh_done_raw;
    if (mesh_done && Kind == WorldCoopKind::Create &&
        CooperativeTerrainMeshesIncomplete(world, Gen.GenPatchMinX, Gen.GenPatchMaxX,
                                           Gen.GenPatchMinZ, Gen.GenPatchMaxZ))
    {
      const ProceduralSettings &settings = world.GetProceduralSettings();
      const int remesh_min_y =
          std::max(settings.MinHeight, settings.SeaLevel - CHUNK_SIZE);
      const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
      const int min_cx = FloorDiv(Gen.GenPatchMinX, CHUNK_SIZE);
      const int max_cx = FloorDiv(Gen.GenPatchMaxX, CHUNK_SIZE);
      const int min_cz = FloorDiv(Gen.GenPatchMinZ, CHUNK_SIZE);
      const int max_cz = FloorDiv(Gen.GenPatchMaxZ, CHUNK_SIZE);
      for (int cx = min_cx; cx <= max_cx; ++cx)
      {
        for (int cz = min_cz; cz <= max_cz; ++cz)
        {
          world.MarkTerrainChunkMeshDirty(glm::ivec3(cx, 0, cz), remesh_min_y,
                                          remesh_max_y);
        }
      }
      mesh_done = false;
    }
    const size_t pending_now =
        world.MeshService->GetDirtyCount() +
        static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
    if (MeshWarmup.MeshWarmupCompletedTotal > MeshWarmup.MeshWarmupProcessedMax)
    {
      MeshWarmup.MeshWarmupProcessedMax = MeshWarmup.MeshWarmupCompletedTotal;
    }
    // Frozen-total substage display: pending can grow mid-phase (re-dirty,
    // relight installs) - the shown total only grows, the fraction never
    // regresses (progress UX U1).
    if (pending_now > MeshWarmup.MeshWarmupSeenTotal)
    {
      MeshWarmup.MeshWarmupSeenTotal = pending_now;
    }
    const size_t display_total =
        MeshWarmupDisplayTotal(MeshWarmup.MeshWarmupStartPending, MeshWarmup.MeshWarmupSeenTotal);
    const float completed_frac =
        MeshWarmupResolvedFraction(pending_now, display_total);
    const float mesh_base =
        Kind == WorldCoopKind::Create
            ? CooperativeCreateMeshProgressBase()
            : (ProceduralFillLoadPath ? ProceduralFillMeshProgressBase()
                                      : CooperativeLoadMeshProgressBase());
    const float mesh_weight =
        Kind == WorldCoopKind::Create
            ? kCreateWeightMeshWarmup
            : (ProceduralFillLoadPath ? kProceduralFillWeightMeshWarmup
                                      : kPhaseWeightMeshWarmup);
    const float mesh_frac =
        mesh_base + mesh_weight * (mesh_done ? 1.0f : completed_frac);
    Report(sink, "mesh_warmup", mesh_frac,
           mesh_done ? "Terrain meshes ready."
                     : FormatMeshWarmupProgress(pending_now, display_total),
           static_cast<int>(MeshWarmupResolved(pending_now, display_total)),
           static_cast<int>(display_total));
    if (mesh_done)
    {
      world.SetLightingRelightDeferred(false);
      world.SetLightingSkylightBulkComplete(false);
      if (Kind == WorldCoopKind::Create)
      {
        BeginPrepareEnter();
      }
      else if (MeshWarmup.MeshWarmupFinalizeOnly)
      {
        // Delay the visual gate until PrepareEnter has restored the player and
        // its terrain column is complete. The pre-gate view tick can then
        // request a cold focus column instead of freezing ingress around a
        // missing column that the visual worklist would treat as N/A.
        BeginPrepareEnter();
      }
      else
      {
        // The gate starts in PrepareView after the saved player's camera
        // column has a terminal load/generation result.
        CurrentPhase = Phase::PostLoadAnalysis;
      }
    }
    else
    {
      const auto warmup_elapsed_ms =
          MeshWarmup.MeshWarmupStartedAt.time_since_epoch().count() == 0
              ? 0
              : std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - MeshWarmup.MeshWarmupStartedAt)
                    .count();
      const bool warmup_wall_timeout =
          warmup_elapsed_ms >= kMeshWarmupMaxWallMs;
      if (MeshWarmup.MeshWarmupTicks >= kMeshWarmupMaxTicks || warmup_wall_timeout)
      {
        const size_t dirty_residual = world.MeshService->GetDirtyCount();
        std::cerr << "MeshWarmup: timeout with pending dirty="
                  << dirty_residual
                  << " ticks=" << MeshWarmup.MeshWarmupTicks
                  << " wall_ms=" << warmup_elapsed_ms << std::endl;
        if (dirty_residual > 0)
        {
          // A35 R0: timeout ≠ MeshWarmup done — stamp residual for AF binding.
          world.GetPhysicsTelemetryMutable().MeshWarmupTimeoutDirtyResidual = 1;
          world.GetPhysicsTelemetryMutable().EnterMeshDirtyResidualN =
              static_cast<int>(
                  std::min<size_t>(dirty_residual, static_cast<size_t>(INT_MAX)));
          MeshWarmup.MeshWarmupTimedOutWithDirty = true;
        }
        world.SetLightingRelightDeferred(false);
        world.SetLightingSkylightBulkComplete(false);
        if (Kind == WorldCoopKind::Create || MeshWarmup.MeshWarmupFinalizeOnly)
        {
          if (MeshWarmup.MeshWarmupFinalizeOnly && !world.IsEnterLitGateActive())
          {
            UEnterLitDiagnostics::BeginSession("cooperative_timeout_finalize");
            world.BeginEnterLitGate();
          }
          BeginPrepareEnter();
        }
        else
        {
          CurrentPhase = Phase::PostLoadAnalysis;
        }
      }
    }
    return;
}

void UWorldCooperativeSession::HandlePhasePostLoadAnalysis(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    // UX U2/S6: existence check - the decision is binary (empty world or
    // not), the full count walked every loaded block in one tick.
    const bool blocksAfterSpatial = world.BlockWorld.HasAnyNonAir();
    NeedsProceduralFill = false;
    if (!blocksAfterSpatial && !world.LoadedFromChunkSave &&
        world.BlockRegistry)
    {
      NeedsProceduralFill = true;
    }
    else if (!blocksAfterSpatial && world.LoadedFromChunkSave)
    {
      if (ChunkFilesRead == 0 && VoxelsFromChunkFiles == 0)
      {
        if (world.IsStreamingEnabled() && world.HasPersistedSave)
        {
          NeedsProceduralFill = false;
        }
        else
        {
          world.HasPersistedSave = false;
          world.LoadedFromChunkSave = false;
          world.AllowProceduralFill = true;
          NeedsProceduralFill = true;
        }
      }
    }
    if (NeedsProceduralFill)
    {
      ProceduralFillLoadPath = true;
      InitGenerationGrid(world, ProceduralFillLoadPath);
      CurrentPhase = Phase::ProceduralFill;
      Report(sink, "generate", ProceduralFillProgressBase(),
             "Generating terrain...");
    }
    else
    {
      CurrentPhase = Phase::FinalizeWorld;
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseProceduralFill(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const bool use_parallel = UseParallelChunkGeneration(world);
    const bool generation_done =
        use_parallel ? AdvanceParallelGeneration(world, budget * CHUNK_SIZE)
                     : AdvanceGeneration(world, budget * CHUNK_SIZE);
    if (generation_done)
    {
      if (!ProceduralFillLoadPath || world.Users.empty())
      {
        // UX U2/S1: spawn resolution moved to the ResolveSpawn phase - the
        // historical one-shot scanned ~2.3k SurfaceYAt candidates in this
        // single tick (multi-second main-thread stall with a frozen bar).
        CurrentPhase = Phase::ResolveSpawn;
      }
      else
      {
        CurrentPhase = Phase::FinalizeWorld;
      }
    }
    const float genFrac =
        ProceduralFillProgressBase() +
        kProceduralFillWeightGenerate *
            (static_cast<float>(Gen.GenDoneColumns) /
             static_cast<float>(std::max(1, Gen.GenTotalColumns)));
    Report(sink, "generate", genFrac, "Generating terrain...",
           Gen.GenDoneColumns, Gen.GenTotalColumns);
    return;
}

void UWorldCooperativeSession::HandlePhasePrepareView(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
  if (Kind == WorldCoopKind::Create)
  {
    HandlePrepareViewCreate(world, sink, budget);
  }
  else
  {
    HandlePrepareViewLoad(world, sink, budget);
  }
  // Shared post-warmup tail (both paths): visible-list warmup, finalize,
  // gate end, done report.
  world.WarmupVisibleListAtCamera();
  WarnIfTerrainMeshesMissing(world, "PrepareView after warmup");
  // Mark before EndEnterLitGate — vis-ready uses the enter snapshot while
  // the gate is still active. Consume happens after GpuWarmup.
  FinalizeCooperativeLoadForEnterGame(world, Kind);
  if (world.IsEnterLitGateActive())
  {
    // Capture the exact pre-exit blockers before EndEnterLitGate changes the
    // visibility snapshot and quiesce state. The following sample records
    // the post-transition state and gate_end edge in the same session.
    const double enter_elapsed_ms =
        StreamingWarmup.StreamingWarmupWallStart.time_since_epoch().count() == 0
            ? world.GetEnterLitGateElapsedMs()
            : std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - StreamingWarmup.StreamingWarmupWallStart)
                  .count();
    EnterLitSample gate_decision_sample{};
    UEnterLitDiagnostics::Sample(world, enter_elapsed_ms,
                                 gate_decision_sample);
    UEnterLitDiagnostics::MaybeLog(gate_decision_sample,
                                   /*frame_index=*/0,
                                   /*every_n_frames=*/1);
    world.EndEnterLitGate();
    EnterLitSample gate_end_sample{};
    UEnterLitDiagnostics::Sample(world, enter_elapsed_ms, gate_end_sample);
    UEnterLitDiagnostics::MaybeLog(gate_end_sample, /*frame_index=*/0,
                                   /*every_n_frames=*/1);
    UEnterLitDiagnostics::EndSession();
  }
  CurrentPhase = Phase::Done;
  Active = false;
  StreamingWarmup.StreamingWarmupTicks = 0;
  StreamingWarmup.StreamingWarmupPeakDebt = 0;
  StreamingWarmup.StreamingWarmupLastRawDebt = 0;
  StreamingWarmup.StreamingWarmupDisplayDebt = 0;
  StreamingWarmup.StreamingWarmupLitWarnLogged = false;
  StreamingWarmup.StreamingWarmupAbortDrainMode = false;
  StreamingWarmup.StreamingWarmupAbortLogged = false;
  StreamingWarmup.StreamingWarmupBestFovDebt = INT_MAX;
  StreamingWarmup.StreamingWarmupLitStallLogged = false;
  const float prepare_view_base =
      Kind == WorldCoopKind::Create
          ? (CooperativeCreateMeshProgressBase() + kCreateWeightMeshWarmup)
          : (ProceduralFillLoadPath
                 ? (ProceduralFillMeshProgressBase() +
                    kProceduralFillWeightMeshWarmup)
                 : CooperativeLoadProgressAfterMesh());
  const float prepare_weight =
      Kind == WorldCoopKind::Create ? kCreateWeightPrepare
                                    : kPhaseWeightPrepareView;
  Report(sink, "prepare_view", prepare_view_base + prepare_weight,
         "Preparing view...");
  Report(sink, "done", 1.f,
         Kind == WorldCoopKind::Create ? "World created." : "World loaded.");
}
void UWorldCooperativeSession::HandlePrepareViewCreate(UWorld &world,
                                                        IUProgressSink &sink,
                                                        int budget)
{
  if (StreamingWarmup.StreamingWarmupTicks == 0)
  {
    WarnIfTerrainMeshesMissing(world, "PrepareView before spawn warmup");
    StreamingWarmup.StreamingWarmupWallStart = std::chrono::steady_clock::now();
    StreamingWarmup.StreamingWarmupPeakDebt = 0;
    StreamingWarmup.StreamingWarmupLastRawDebt = 0;
    StreamingWarmup.StreamingWarmupDisplayDebt = 0;
    StreamingWarmup.StreamingWarmupLitWarnLogged = false;
    StreamingWarmup.StreamingWarmupBestFovDebt = INT_MAX;
    StreamingWarmup.StreamingWarmupLitProgressAt = StreamingWarmup.StreamingWarmupWallStart;
    StreamingWarmup.StreamingWarmupLitStallLogged = false;
    if (!world.IsEnterLitGateActive())
    {
      UEnterLitDiagnostics::BeginSession("cooperative_create_prepare_view");
      world.BeginEnterLitGate();
    }
  }
  TickCreateSpawnMeshWarmup(world, std::max(1, budget / 2));
  // Era41: light LitDrawable FOV on create bar (async Capture + workers).
  world.TickEnterFovLitPass(
      std::max(1, URuntimeTuning::Get().EnterFovLitCaptureBudget));
  ++StreamingWarmup.StreamingWarmupTicks;
  bool underfeet_lit = false;
  const int raw_debt = world.CountCreateNearFovWarmupDebt(&underfeet_lit);
  const int fov_debt = world.CountEnterFovLitDebt();
  if (fov_debt < StreamingWarmup.StreamingWarmupBestFovDebt)
  {
    StreamingWarmup.StreamingWarmupBestFovDebt = fov_debt;
    StreamingWarmup.StreamingWarmupLitProgressAt = std::chrono::steady_clock::now();
  }
  const auto &phys = world.GetPhysicsTelemetry();
  const int debt = raw_debt + fov_debt + phys.FocusDarkMesh +
                   phys.SoftDeferEmptyPlaceholderN;
  if (debt > StreamingWarmup.StreamingWarmupPeakDebt)
  {
    StreamingWarmup.StreamingWarmupPeakDebt = debt;
  }
  // Era35 P3: monotonic display debt — only decreases, never jumps up.
  StreamingWarmup.StreamingWarmupDisplayDebt =
      StreamingWarmup.StreamingWarmupTicks == 1
          ? debt
          : MonotonicDisplayDebt(StreamingWarmup.StreamingWarmupDisplayDebt, debt);
  StreamingWarmup.StreamingWarmupLastRawDebt = debt;
  const bool spawn_settled = IsCreateSpawnWarmupSettled(world);
  const float prepare_view_base =
      CooperativeCreateMeshProgressBase() + kCreateWeightMeshWarmup;
  const int denom = std::max(1, StreamingWarmup.StreamingWarmupPeakDebt);
  const float stream_inner =
      spawn_settled ? 1.0f
                    : (1.0f - CreateBarDebtFraction(
                                  StreamingWarmup.StreamingWarmupDisplayDebt, denom));
  const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() -
                                StreamingWarmup.StreamingWarmupWallStart)
                                .count();
  const double ms_since_lit_progress =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - StreamingWarmup.StreamingWarmupLitProgressAt)
          .count();
  const bool lit_progress_stalled = EnterLitDebtProgressStalled(
      fov_debt, StreamingWarmup.StreamingWarmupBestFovDebt, underfeet_lit,
      ms_since_lit_progress,
      static_cast<double>(EnterLitProgressStallMs()), elapsed_ms,
      static_cast<double>(CreateSpawnWarmupSoftWallMs()));
  if (lit_progress_stalled && !StreamingWarmup.StreamingWarmupLitStallLogged)
  {
    StreamingWarmup.StreamingWarmupLitStallLogged = true;
    std::cerr << "[LitRing] create lit progress-stall abort (" << elapsed_ms
              << "ms, lit=" << fov_debt
              << ", best=" << StreamingWarmup.StreamingWarmupBestFovDebt << ")\n";
  }
  EnterLitSample lit_sample{};
  UEnterLitDiagnostics::Sample(world, elapsed_ms, lit_sample);
  UEnterLitDiagnostics::MaybeLog(lit_sample, StreamingWarmup.StreamingWarmupTicks);
  std::string status;
  if (spawn_settled)
  {
    status = "Preparing view...";
  }
  else if (fov_debt > 0)
  {
    status = "Lighting… " + std::to_string(fov_debt) + " left";
  }
  else
  {
    status = "Loading FOV… " +
             std::to_string(StreamingWarmup.StreamingWarmupDisplayDebt) + " left";
  }
  Report(sink, "prepare_view",
         prepare_view_base + kCreateWeightPrepare * stream_inner, status);
  // LitRing C: RequireZero holds until debt clears OR progress stall / wall.
  const bool require_zero = URuntimeTuning::Get().EnterLitRequireZero;
  if (spawn_settled)
  {
    // fall through to Finalize
  }
  else if (fov_debt > 0)
  {
    if (!ShouldHoldEnterBarForFovLit(
            fov_debt, elapsed_ms,
            URuntimeTuning::Get().EnterFovLitHardWallMs, require_zero,
            lit_progress_stalled))
    {
      std::cerr << "[LitRing] create lit leave (" << elapsed_ms
                << "ms, lit=" << fov_debt
                << ", stall=" << (lit_progress_stalled ? 1 : 0) << ")\n";
    }
    else
    {
      if (!StreamingWarmup.StreamingWarmupLitWarnLogged &&
          elapsed_ms >=
              static_cast<double>(
                  URuntimeTuning::Get().EnterFovLitHardWallMs))
      {
        StreamingWarmup.StreamingWarmupLitWarnLogged = true;
        std::cerr << "[Era42] create lit still draining past warn wall ("
                  << elapsed_ms << "ms, lit=" << fov_debt << ")\n";
      }
      return;
    }
  }
  else if (ShouldSoftLeaveCreateSpawnWarmup(underfeet_lit, elapsed_ms))
  {
    std::cerr << "[Era34] create spawn soft-wall after underfeet lit ("
              << elapsed_ms << "ms, debt=" << debt << ")\n";
  }
  else if (ShouldHardLeaveCreateSpawnWarmup(elapsed_ms,
                                            StreamingWarmup.StreamingWarmupTicks))
  {
    std::cerr << "[Era34] create spawn hard ceiling (" << elapsed_ms
              << "ms, ticks=" << StreamingWarmup.StreamingWarmupTicks << ", debt=" << debt
              << ")\n";
  }
  else
  {
    return;
  }
}
void UWorldCooperativeSession::HandlePrepareViewLoad(UWorld &world,
                                                      IUProgressSink &sink,
                                                      int budget)
{
  if (!world.IsEnterLitGateActive())
  {
    if (!world.IsEnterFocusTerrainReady())
    {
      const glm::ivec3 focus = UChunkManager::WorldToChunk(
          world.GetPreferredLoadFocusBlock());
      world.TickEnterStreamingWarmup(std::max(1, budget / 2));
      Report(sink, "prepare_view",
             ProceduralFillLoadPath
                 ? (ProceduralFillMeshProgressBase() +
                    kProceduralFillWeightMeshWarmup)
                 : CooperativeLoadProgressAfterMesh(),
             "Loading player terrain (" + std::to_string(focus.x) + "," +
                 std::to_string(focus.z) + ")...");
      return;
    }
    UEnterLitDiagnostics::BeginSession("cooperative_load_prepare");
    world.BeginEnterLitGate();
  }
  WarnIfTerrainMeshesMissing(world, "PrepareView before warmup");
  if (world.IsEnterLitGateActive())
  {
    if (StreamingWarmup.StreamingWarmupTicks == 0)
    {
      StreamingWarmup.StreamingWarmupWallStart = std::chrono::steady_clock::now();
      StreamingWarmup.StreamingWarmupPeakDebt = 0;
      StreamingWarmup.StreamingWarmupLastRawDebt = 0;
      StreamingWarmup.StreamingWarmupDisplayDebt = 0;
      StreamingWarmup.StreamingWarmupLitWarnLogged = false;
      StreamingWarmup.StreamingWarmupAbortDrainMode = false;
      StreamingWarmup.StreamingWarmupAbortLogged = false;
      StreamingWarmup.StreamingWarmupAbortCapLogged = false;
      StreamingWarmup.StreamingWarmupSettleLogged = false;
      StreamingWarmup.StreamingWarmupBestFovDebt = INT_MAX;
      StreamingWarmup.StreamingWarmupLitProgressAt = StreamingWarmup.StreamingWarmupWallStart;
      StreamingWarmup.StreamingWarmupLitStallLogged = false;
    }
    EnterWarmupStepSample step_sample{};
    const auto &phys_before = world.GetPhysicsTelemetry();
    const int relight_completed_before = phys_before.RelightCompletedN;
    const int gpu_finish_before = phys_before.GpuFinishN;
    // Era46/47: shared enter drain frame (sets EnterLitQuiesce latch +
    // DrainEnterGameMeshWarmup + TickEnterGateMeshDrain).
    constexpr int kCoopEnterMeshBudget = EnterWarmupMeshBudgetDefault();
    const int gate_iters =
        std::max(1, URuntimeTuning::Get().EnterGateMeshDrainIterations);
    {
      const auto t0 = std::chrono::high_resolution_clock::now();
      world.TickEnterWarmupDrainFrame(kCoopEnterMeshBudget, gate_iters,
                                      12.0);
      const double drain_frame_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - t0)
              .count();
      // Approximate split for profile (gate iters dominate when lit).
      step_sample.drain_mesh_ms = drain_frame_ms * 0.35;
      step_sample.gate_drain_ms = drain_frame_ms * 0.65;
    }
    {
      const auto t0 = std::chrono::high_resolution_clock::now();
      world.TickEnterFovLitPass(
          std::max(1, URuntimeTuning::Get().EnterFovLitCaptureBudget));
      step_sample.lit_pass_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - t0)
              .count();
    }
    ++StreamingWarmup.StreamingWarmupTicks;
    const int fov_debt = world.CountEnterFovLitDebt();
    if (fov_debt < StreamingWarmup.StreamingWarmupBestFovDebt)
    {
      StreamingWarmup.StreamingWarmupBestFovDebt = fov_debt;
      StreamingWarmup.StreamingWarmupLitProgressAt = std::chrono::steady_clock::now();
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  StreamingWarmup.StreamingWarmupWallStart)
                                  .count();
    const double ms_since_lit_progress =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - StreamingWarmup.StreamingWarmupLitProgressAt)
            .count();
    EnterLitSample lit_sample{};
    UEnterLitDiagnostics::Sample(world, elapsed_ms, lit_sample);
    UEnterLitDiagnostics::MaybeLog(lit_sample, StreamingWarmup.StreamingWarmupTicks);
    UEnterLitDiagnostics::MaybeLogHeartbeat(lit_sample, 2000.0);
    const auto &phys_after = world.GetPhysicsTelemetry();
    step_sample.relight_drain_ms = phys_after.RelightDrainMs;
    step_sample.mesh_emerge_ms = phys_after.MeshEmergeMs;
    step_sample.mesh_immediate_ms = phys_after.MeshImmediateMs;
    step_sample.relight_completed_delta =
        phys_after.RelightCompletedN - relight_completed_before;
    step_sample.gpu_finish_delta =
        phys_after.GpuFinishN - gpu_finish_before;
    UEnterLitDiagnostics::RecordFrameSteps(step_sample);
    const int combined_debt =
        EnterWarmupCombinedDebt(lit_sample, fov_debt);
    if (combined_debt > StreamingWarmup.StreamingWarmupPeakDebt)
    {
      StreamingWarmup.StreamingWarmupPeakDebt = combined_debt;
    }
    StreamingWarmup.StreamingWarmupDisplayDebt =
        StreamingWarmup.StreamingWarmupTicks == 1
            ? combined_debt
            : MonotonicDisplayDebt(StreamingWarmup.StreamingWarmupDisplayDebt,
                                   combined_debt);
    StreamingWarmup.StreamingWarmupLastRawDebt = combined_debt;
    const float prepare_view_base =
        ProceduralFillLoadPath
            ? (ProceduralFillMeshProgressBase() +
               kProceduralFillWeightMeshWarmup)
            : CooperativeLoadProgressAfterMesh();
    const int denom = std::max(1, StreamingWarmup.StreamingWarmupPeakDebt);
    const bool ring_ready = world.IsSpawnMeshRingReady();
    const bool visibility_ready = world.IsEnterVisibilityReady();
    const int visibility_debt = lit_sample.visibility_debt;
    const bool mesh_blockers_clear = !world.NeedsEnterGameMeshWarmup();
    const bool underfeet_present = world.IsEnterUnderfeetPresentReady();
    const glm::ivec3 underfeet_center =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    const int underfeet_gpu_pending =
        world.GetMeshService().CountPendingGpuAppliesInHorizontalRadius(
            underfeet_center, 1);
    const bool underfeet_mesh_ok =
        underfeet_present ||
        !world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(),
            glm::ivec3(underfeet_center.x, 0, underfeet_center.z), 1);
    // Phase 5.7R4: UF Dirty carve before soft_force wall (existing budgets).
    if (ShouldCarveUnderfeetBeforeSoftForce(
            underfeet_present, elapsed_ms,
            URuntimeTuning::Get().EnterForceInGameMs))
    {
      world.MarkSpawnRingUnfinishedDirty(8);
    }
    // Phase 5.4.1: no Quiesce bypass while visibility_debt>0.
    const bool ring_ready_for_exit = EnterRingReadyForExit(
        ring_ready, world.IsEnterSessionActive(), underfeet_present,
        underfeet_gpu_pending, underfeet_mesh_ok, visibility_debt);
    const bool visibility_ready_for_exit = EnterVisibilityReadyForExit(
        visibility_ready, world.IsEnterSessionActive(), underfeet_present,
        fov_debt, underfeet_gpu_pending, StreamingWarmup.StreamingWarmupAbortDrainMode,
        elapsed_ms, URuntimeTuning::Get().EnterMeshAbortMs,
        visibility_debt);
    const bool lit_progress_stalled = EnterLitDebtProgressStalled(
        fov_debt, StreamingWarmup.StreamingWarmupBestFovDebt, underfeet_present,
        ms_since_lit_progress,
        static_cast<double>(EnterLitProgressStallMs()), elapsed_ms,
        static_cast<double>(CreateSpawnWarmupSoftWallMs()));
    if (lit_progress_stalled && !StreamingWarmup.StreamingWarmupLitStallLogged)
    {
      StreamingWarmup.StreamingWarmupLitStallLogged = true;
      LOG(WARNING) << "[LitRing] load lit progress-stall abort elapsed_ms="
                   << elapsed_ms << " lit=" << fov_debt
                   << " best=" << StreamingWarmup.StreamingWarmupBestFovDebt;
      CubatariumFlushLogs();
    }
    if (lit_progress_stalled)
    {
      StreamingWarmup.StreamingWarmupAbortDrainMode = true;
    }
    const bool abort_underfeet_cap = ShouldReleaseEnterAfterAbortUnderfeetCap(
        StreamingWarmup.StreamingWarmupAbortDrainMode, elapsed_ms,
        URuntimeTuning::Get().EnterForceInGameMs, underfeet_present,
        underfeet_gpu_pending);
    const bool soft_exit_cap = ShouldForceEnterLoadSoftExit(
        StreamingWarmup.StreamingWarmupAbortDrainMode, elapsed_ms,
        URuntimeTuning::Get().EnterForceInGameMs, fov_debt);
    const bool soft_clean_cap = ShouldForceEnterLoadSoftCleanDebt(
        elapsed_ms, combined_debt, underfeet_present);
    // LitRing C: stall with underfeet → settle with FOV holes OK (finite load).
    // Phase5 S4: soft_exit_cap settles without underfeet (stuck missing mesh).
    // Phase 5.2.0: soft_clean_cap settles when debt=0 + underfeet @12s.
    // A35 R0: soft settle blocked while MeshService dirty residual high
    // (EnterLit mesh_dirty can false-clear hinterland).
    const size_t mesh_service_dirty_n =
        world.MeshService ? world.MeshService->GetDirtyCount() : 0;
    // Telemetry stamp MeshWarmup.MeshWarmupTimedOutWithDirty stays for AF; soft settle
    // blocks only while residual dirty is still above cap (drain can clear).
    const bool dirty_residual_blocks_soft =
        EnterMeshDirtyResidualBlocksSoftSettle(mesh_service_dirty_n);
    if (dirty_residual_blocks_soft &&
        (soft_exit_cap || soft_clean_cap) &&
        world.GetPhysicsTelemetryMutable()
                .EnterSoftSettleBlockedDirtyResidual == 0)
    {
      world.GetPhysicsTelemetryMutable()
          .EnterSoftSettleBlockedDirtyResidual = 1;
      world.GetPhysicsTelemetryMutable().EnterMeshDirtyResidualN =
          static_cast<int>(std::min<size_t>(
              mesh_service_dirty_n, static_cast<size_t>(INT_MAX)));
      LOG(WARNING) << "[EnterWarmup] soft_settle_blocked_dirty_residual n="
                   << mesh_service_dirty_n
                   << " warmup_timeout_dirty="
                   << (MeshWarmup.MeshWarmupTimedOutWithDirty ? 1 : 0)
                   << " elapsed_ms=" << elapsed_ms;
      CubatariumFlushLogs();
    }
    const bool soft_exit_ok =
        soft_exit_cap && !dirty_residual_blocks_soft;
    const bool soft_clean_ok =
        soft_clean_cap && !dirty_residual_blocks_soft;
    const bool live_blockers_ok =
        ring_ready_for_exit && visibility_ready_for_exit &&
        mesh_blockers_clear;
    const bool load_settled =
        live_blockers_ok ||
        (StreamingWarmup.StreamingWarmupAbortDrainMode && underfeet_present &&
         underfeet_gpu_pending <= 0 && fov_debt <= 0 &&
         !dirty_residual_blocks_soft) ||
        (lit_progress_stalled && underfeet_present &&
         underfeet_gpu_pending <= 0 && !dirty_residual_blocks_soft) ||
        (abort_underfeet_cap && underfeet_present &&
         underfeet_gpu_pending <= 0 &&
         (fov_debt <= 0 || lit_progress_stalled) &&
         !dirty_residual_blocks_soft) ||
        soft_clean_ok || soft_exit_ok;
    if (load_settled && !StreamingWarmup.StreamingWarmupSettleLogged)
    {
      StreamingWarmup.StreamingWarmupSettleLogged = true;
      const char *settle_reason = "live_blockers";
      if (!live_blockers_ok)
      {
        if (soft_clean_ok)
        {
          settle_reason = "soft_clean";
        }
        else if (soft_exit_ok)
        {
          // Phase 5.7R5: soft_force settle_reason only with UF presentable.
          settle_reason = ShouldAllowEnterSoftForceSettle(underfeet_present)
                              ? "soft_force"
                              : "force_ingame_no_uf";
        }
        else if (abort_underfeet_cap)
        {
          settle_reason = "abort_underfeet";
        }
        else if (lit_progress_stalled)
        {
          settle_reason = "lit_stall";
        }
        else
        {
          settle_reason = "abort_drain";
        }
      }
      world.SetLastEnterSettleReason(settle_reason);
      LOG(INFO) << "[EnterWarmup] settle_reason=" << settle_reason
                << " elapsed_ms=" << elapsed_ms
                << " combined_debt=" << combined_debt
                << " needs_mesh=" << (mesh_blockers_clear ? 0 : 1)
                << " mesh_service_dirty=" << mesh_service_dirty_n
                << " underfeet=" << (underfeet_present ? 1 : 0)
                << " ring_ready=" << (ring_ready ? 1 : 0)
                << " visibility_debt=" << visibility_debt;
      if (std::strcmp(settle_reason, "soft_force") == 0 &&
          visibility_debt > 0)
      {
        world.GetPhysicsTelemetryMutable().EnterSettleSoftForceWithDebt = 1;
        world.BeginEnterGameMeshBurst(24);
        world.MarkSpawnRingUnfinishedDirty(8);
      }
      else if (std::strcmp(settle_reason, "force_ingame_no_uf") == 0)
      {
        world.BeginEnterGameMeshBurst(24);
        world.MarkSpawnRingUnfinishedDirty(16);
      }
      CubatariumFlushLogs();
    }
    if ((abort_underfeet_cap || lit_progress_stalled || soft_exit_ok ||
         soft_clean_ok) &&
        load_settled && !StreamingWarmup.StreamingWarmupAbortCapLogged)
    {
      StreamingWarmup.StreamingWarmupAbortCapLogged = true;
      LOG(WARNING) << "[EnterWarmup] coop_abort_underfeet_cap elapsed_ms="
                   << elapsed_ms << " ring_ready=" << (ring_ready ? 1 : 0)
                   << " visibility_debt=" << visibility_debt
                   << " lit_stall=" << (lit_progress_stalled ? 1 : 0)
                   << " soft_exit=" << (soft_exit_ok ? 1 : 0)
                   << " soft_clean=" << (soft_clean_ok ? 1 : 0)
                   << " underfeet=" << (underfeet_present ? 1 : 0);
      CubatariumFlushLogs();
    }
    if (!StreamingWarmup.StreamingWarmupAbortDrainMode &&
        ShouldForceEnterMeshAbort(fov_debt, ring_ready, elapsed_ms,
                                  URuntimeTuning::Get().EnterMeshAbortMs))
    {
      StreamingWarmup.StreamingWarmupAbortDrainMode = true;
      if (!StreamingWarmup.StreamingWarmupAbortLogged)
      {
        StreamingWarmup.StreamingWarmupAbortLogged = true;
        LOG(INFO) << "[EnterWarmup] coop_abort_drain elapsed_ms="
                  << elapsed_ms << " dirty="
                  << (lit_sample.mesh_dirty ? 1 : 0)
                  << " gpu_pending=" << lit_sample.mesh_gpu_pending_near
                  << " fifo=" << lit_sample.fifo_n << " ring="
                  << (ring_ready ? 1 : 0)
                  << " visibility_debt=" << visibility_debt;
        CubatariumFlushLogs();
      }
    }
    // Era46 C: escalate GPU drain only after abort_drain ≥3 min — same
    // pipeline, higher budget; does not weaken gate.
    // Gate-active GPU consume is TickEnterGateMeshDrain only (one consume).
    if (ShouldEscalateEnterWarmupGpuDrain(StreamingWarmup.StreamingWarmupAbortDrainMode,
                                          elapsed_ms) &&
        world.NeedsEnterGameMeshWarmup() &&
        !world.IsEnterLitGateActive())
    {
      world.DrainEnterGameMeshWarmup(kCoopEnterMeshBudget * 2);
    }
    const float stream_inner =
        load_settled
            ? 1.0f
            : (1.0f - CreateBarDebtFraction(StreamingWarmup.StreamingWarmupDisplayDebt,
                                            denom));
    const std::string status = BuildEnterWarmupStatus(
        lit_sample, fov_debt, ring_ready, StreamingWarmup.StreamingWarmupAbortDrainMode,
        elapsed_ms, URuntimeTuning::Get().EnterFovLitHardWallMs,
        visibility_debt);
    Report(sink, "prepare_view",
           prepare_view_base + kPhaseWeightPrepareView * stream_inner,
           status);
    if (!load_settled)
    {
      return;
    }
    UEnterLitDiagnostics::MaybeLogProfileSummary(lit_sample);
  }
}

void UWorldCooperativeSession::HandlePhaseDrainAsyncIo(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    // First frame: hard-cancel leftover gen + stale pending IO maps so drain
    // cannot wait forever on cancelled jobs.
    if (Save.SaveDrainIoFrames == 0)
    {
      if (world.Streaming)
      {
        world.Streaming->CancelChunkGeneration();
      }
      if (world.Persistence)
      {
        (void)world.Persistence->AbortAsyncChunkIoFor(
            std::chrono::milliseconds(0));
      }
      CubatariumLogInfo("Save", "drain_async_io begin");
    }
    bool drained = true;
    if (world.Persistence)
    {
      drained = world.Persistence->TickDrainAsyncChunkIo(
          world, std::max(budget * 2, 8));
    }
    ++Save.SaveDrainIoFrames;
    const int max_drain_frames =
        world.BackgroundQuiesceFinished ? 4 : 24;
    const float frac = std::min(
        0.04f, 0.04f * static_cast<float>(Save.SaveDrainIoFrames) /
                    static_cast<float>(std::max(1, max_drain_frames)));
    Report(sink, "init", frac, "Flushing pending terrain IO...");
    if (drained || Save.SaveDrainIoFrames >= max_drain_frames)
    {
      if (!drained && world.Persistence)
      {
        (void)world.Persistence->AbortAsyncChunkIoFor(
            std::chrono::milliseconds(0));
      }
      CubatariumLogInfo(
          "Save",
          std::string("drain_async_io end frames=") +
              std::to_string(Save.SaveDrainIoFrames) +
              " drained=" + (drained ? "1" : "0"));
      CurrentPhase = Phase::ScanSaveChunks;
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseSaveChunks(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    if (!world.BlockRegistry)
    {
      CurrentPhase = Phase::SaveMetadataMarker;
      return;
    }
    const bool save_by_ground_column = Save.SaveUsesTerrainColumns;
    int saved = 0;
    while (Save.SaveChunkIndex < Save.SaveChunkCoords.size() && saved < budget)
    {
      const glm::ivec3 coord = Save.SaveChunkCoords[Save.SaveChunkIndex++];
      if (save_by_ground_column)
      {
        if (IsTerrainChunkComplete(world.BlockWorld, coord,
                                   world.ProceduralTemplate.MaxHeight,
                                   kNoTerrainChunkSlice,
                                   world.ProceduralTemplate.MinHeight))
        {
          world.GetChunkStorage().SaveTerrainColumn(
              coord, world.BlockWorld, FolderPath, *world.BlockRegistry,
              world.ProceduralTemplate.MaxHeight,
              world.ProceduralTemplate.MinHeight);
        }
        else
        {
          world.GetChunkStorage().RemoveTerrainColumnFromDisk(
              FolderPath, coord, world.ProceduralTemplate.MaxHeight, nullptr,
              world.ProceduralTemplate.MinHeight);
        }
      }
      else
      {
        const UChunk *chunk = world.BlockWorld.GetChunkManager().GetChunk(coord);
        if (chunk)
        {
          world.GetChunkStorage().SaveChunk(coord, *chunk, FolderPath,
                                            *world.BlockRegistry);
        }
      }
      ++saved;
    }
    const float frac =
        Save.SaveChunkCoords.empty()
            ? 0.85f
            : 0.05f + 0.8f * (static_cast<float>(Save.SaveChunkIndex) /
                               static_cast<float>(Save.SaveChunkCoords.size()));
    Report(sink, "chunks", frac,
           "Saving chunks (" + std::to_string(Save.SaveChunkIndex) + "/" +
               std::to_string(Save.SaveChunkCoords.size()) + ")...",
           static_cast<int>(Save.SaveChunkIndex),
           static_cast<int>(Save.SaveChunkCoords.size()));
    if (Save.SaveChunkIndex >= Save.SaveChunkCoords.size())
    {
      CurrentPhase = Phase::SaveMetadataMarker;
    }
    return;
}

void UWorldCooperativeSession::HandlePhaseGenerateColumns(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    const bool use_parallel = UseParallelChunkGeneration(world);
    const bool generation_done =
        use_parallel ? AdvanceParallelGeneration(world, budget * CHUNK_SIZE)
                     : AdvanceGeneration(world, budget * CHUNK_SIZE);
    if (generation_done)
    {
      // UX U2/S1: spawn resolution moved to the ResolveSpawn phase.
      CurrentPhase = Phase::ResolveSpawn;
    }
    const float frac =
        kCreateWeightInit +
        kCreateWeightGenerate *
            (static_cast<float>(Gen.GenDoneColumns) /
             static_cast<float>(std::max(1, Gen.GenTotalColumns)));
    Report(sink, "generate", frac, "Generating terrain...");
    return;
}

void UWorldCooperativeSession::HandlePhaseResolveSpawn(UWorld &world,
                                                       IUProgressSink &sink,
                                                       int budget)
{
  if (!world.WorldGen)
  {
    CurrentPhase = (Kind == WorldCoopKind::Create) ? Phase::PostCreate
                                                   : Phase::FinalizeWorld;
    return;
  }
  glm::vec3 resolved = world.SpawnPoint;
  if (world.BlockRegistry)
  {
    // Nearest-first candidate probes; a multi-second one-tick scan is now
    // spread over frames with live progress (UX U2/S1).
    world.WorldGen->TickSpawnResolve(world.BlockWorld, *world.BlockRegistry,
                                     0, 0, 64, resolved);
  }
  else
  {
    resolved = world.WorldGen->DefaultSpawnPosition(0, 0);
  }
  if (world.WorldGen->SpawnResolveFinished())
  {
    world.SpawnPoint = resolved;
    CurrentPhase = (Kind == WorldCoopKind::Create) ? Phase::PostCreate
                                                   : Phase::FinalizeWorld;
  }
  Report(sink, "spawn", -1.f, "Finding spawn point...");
}

void UWorldCooperativeSession::HandlePhasePostCreate(UWorld &world,
                                             IUProgressSink &sink,
                                             int budget)
{
    // UX U2/S5: seal one chunk row per tick; the whole-patch sweep used to
    // run in this single tick.
    if (!SealFluidInGenerationPatchRow(world))
    {
      Report(sink, "post_create", -1.f, "Sealing shorelines...");
      return;
    }
    world.WorldName = TargetWorldName;
    world.WorldGenSetsData = BuildDefaultWorldGenSets();
    world.RebuildResolvedObjectFeatures();
    world.RebuildWorldGenPipeline();
    world.AllowProceduralFill = world.IsStreamingEnabled();
    world.InitStreamerCallbacks();
    if (world.Streaming->HasStreamer())
    {
      world.Streaming->MarkPersistedColumnsFromWorld();
    }
    if (PrepareCreateVisuals)
    {
      BeginMeshWarmup(world);
    }
    else if (world.IsLightingRelightDeferred() && world.BlockRegistry)
    {
      // CLI generation persists voxel and lighting data, not render caches or
      // a camera-ready view. Keep full terrain relighting, then finish without
      // scheduling the create-only mesh/view warmup.
      BeginDeferredRelightQueue(world);
    }
    else
    {
      FinishHeadlessCreate(world);
    }
    if (CurrentPhase == Phase::MeshWarmup)
    {
      Report(sink, "mesh_warmup", CooperativeCreateMeshProgressBase(),
             "Building terrain meshes...");
    }
    return;
}

} // namespace cutum
