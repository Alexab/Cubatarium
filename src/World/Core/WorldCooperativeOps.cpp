#include "World/Core/WorldCooperativeOps.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Core/RuntimeTuning.h"
#include "App/Platform/Log.h"
#include "glog/logging.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/WorldStreaming.h"
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
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>

namespace cutum
{

namespace
{

void ParkSpawnRingMeshWhileRelightDeferred(UWorld &world)
{
  if (!world.IsLightingRelightDeferred())
  {
    return;
  }
  const auto &phys = world.GetPhysicsTelemetry();
  if (ShouldSkipParkSpawnRingForMissHeal(
          world.NeedsSpawnRingCatchUp(), phys.FocusMissingMesh != 0,
          phys.MissHoriz))
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
  const int cy0 = std::max(0, FloorDiv(sea - CHUNK_SIZE, CHUNK_SIZE));
  const int cy1 = FloorDiv(sea + CHUNK_SIZE * 2, CHUNK_SIZE);
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
    world.Persistence->ClearPendingRelights();
  }
  BeginColumnRelightQueue(world);
}

void UWorldCooperativeSession::BeginBulkChunkRelightQueue(UWorld &world)
{
  BulkRelightChunkQueue.clear();
  BulkRelightChunkScheduledIndex = 0;
  BulkRelightChunkAppliedCount = 0;
  ColumnRelightQueue.clear();
  ColumnRelightIndex = 0;
  ColumnRelightScheduledIndex = 0;
  ColumnRelightAppliedCount = 0;
  RelightQueue.clear();
  RelightQueueIndex = 0;

  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      { BulkRelightChunkQueue.push_back(chunk.GetCoord()); });

  world.SetLightingRelightDeferred(false);
  world.SetLightingSkylightBulkComplete(true);

  std::cout << "[WorldLoad] RelightBulkChunks: "
            << BulkRelightChunkQueue.size() << " chunks" << std::endl;

  if (BulkRelightChunkQueue.empty())
  {
    BeginEmissiveBlockLightQueue(world);
    return;
  }

  CurrentPhase = Phase::RelightBulkChunks;
}

void UWorldCooperativeSession::BeginColumnRelightQueue(UWorld &world)
{
  ColumnRelightQueue.clear();
  ColumnRelightIndex = 0;
  ColumnRelightScheduledIndex = 0;
  ColumnRelightAppliedCount = 0;

  std::unordered_set<glm::ivec3, IVec3Hash> ground_columns;
  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        const glm::ivec3 ground(coord.x, 0, coord.z);
        if (ground_columns.insert(ground).second)
        {
          ColumnRelightQueue.push_back(
              glm::ivec2(ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE));
        }
      });

  world.SetLightingRelightDeferred(false);
  world.SetLightingSkylightBulkComplete(true);
  RelightQueue.clear();
  RelightQueueIndex = 0;

  std::cout << "[WorldLoad] RelightColumns: " << ColumnRelightQueue.size()
            << " terrain columns" << std::endl;

  if (ColumnRelightQueue.empty())
  {
    BeginEmissiveBlockLightQueue(world);
    return;
  }

  RelightColumnsStartedAt = std::chrono::steady_clock::now();
  CurrentPhase = Phase::RelightColumns;
}

void UWorldCooperativeSession::FinishEmissiveBlockLightRelight(UWorld &world)
{
  std::cout << "[WorldLoad] RelightEmissiveBlockLight done: "
            << EmissiveChunkRelightQueue.size() << " chunks" << std::endl;
  EmissiveChunkRelightQueue.clear();
  EmissiveChunkRelightIndex = 0;
  world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
  BeginMeshWarmupInner(world);
}

void UWorldCooperativeSession::BeginEmissiveBlockLightQueue(UWorld &world)
{
  EmissiveChunkRelightQueue.clear();
  EmissiveChunkRelightIndex = 0;
  ColumnRelightQueue.clear();
  ColumnRelightIndex = 0;
  ColumnRelightScheduledIndex = 0;
  ColumnRelightAppliedCount = 0;

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
          bool has_emissive = false;
          for (int ly = 0; ly < CHUNK_SIZE && !has_emissive; ++ly)
          {
            for (int lz = 0; lz < CHUNK_SIZE && !has_emissive; ++lz)
            {
              for (int lx = 0; lx < CHUNK_SIZE; ++lx)
              {
                const BlockId id =
                    chunk.GetBlockLocal(glm::ivec3(lx, ly, lz));
                if (world.BlockRegistry->GetLightEmission(id) > 0)
                {
                  has_emissive = true;
                  break;
                }
              }
            }
          }
          if (has_emissive)
          {
            EmissiveChunkRelightQueue.push_back(chunk.GetCoord());
          }
        });
  }

  std::cout << "[WorldLoad] RelightEmissiveBlockLight: "
            << EmissiveChunkRelightQueue.size() << " chunks" << std::endl;

  if (EmissiveChunkRelightQueue.empty())
  {
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
  if (MeshWarmupFinalizeOnly)
  {
    // Era51: continue residual drain — do not MarkAllDirty the whole world again.
    if (MeshWarmupTicks == 0)
    {
      MeshWarmupStartedAt = std::chrono::steady_clock::now();
      MeshWarmupStartPending = std::max<size_t>(1, pending_now);
      MeshWarmupCompletedTotal = 0;
      MeshWarmupProcessedMax = 0;
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
      const int remesh_min_y = std::max(0, settings.SeaLevel - CHUNK_SIZE);
      const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
      const int min_cx = FloorDiv(GenPatchMinX, CHUNK_SIZE);
      const int max_cx = FloorDiv(GenPatchMaxX, CHUNK_SIZE);
      const int min_cz = FloorDiv(GenPatchMinZ, CHUNK_SIZE);
      const int max_cz = FloorDiv(GenPatchMaxZ, CHUNK_SIZE);
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
  MeshWarmupTicks = 0;
  MeshWarmupStartedAt = {};
  MeshWarmupProcessedMax = 0;
  MeshWarmupCompletedTotal = 0;
  MeshWarmupStartPending =
      world.MeshService->GetDirtyCount() +
      static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
  if (MeshWarmupStartPending == 0)
  {
    MeshWarmupStartPending = 1;
  }
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
             std::to_string(MeshWarmupStartPending));
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
  case Phase::SaveMetadata:
    return "save_metadata";
  case Phase::GenerateColumns:
    return "generate_columns";
  case Phase::PostCreate:
    return "post_create";
  case Phase::Done:
    return "done";
  }
  return "unknown";
}

void UWorldCooperativeSession::Report(IUProgressSink &sink,
                                      const std::string &phaseId, float fraction,
                                      const std::string &message) const
{
  sink.Report(phaseId, fraction, message);
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
  StreamingWarmupTicks = 0;
  StreamingWarmupPeakDebt = 0;
  Report(sink, "done", 1.f, "World loaded.");
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
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Load;
  Active = true;
  FolderPath = world_folder_path;
  CurrentPhase = Phase::Init;
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
  (void)world;
}

void UWorldCooperativeSession::BeginCreate(UWorld &world,
                                           const std::string &world_name)
{
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Create;
  Active = true;
  TargetWorldName = world_name;
  CurrentPhase = Phase::Init;
  (void)world;
}

void UWorldCooperativeSession::ScanChunkFiles(UWorld &world)
{
  ChunkFiles.clear();
  ChunkFileIndex = 0;
  const std::string chunks_dir = FolderPath + "/chunks";
  if (!SpatialStreamingLoad && std::filesystem::exists(chunks_dir))
  {
    for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
    {
      const auto ext = entry.path().extension();
      if (ext != ".json" && ext != ".cchunk")
      {
        continue;
      }
      ChunkFiles.push_back(entry.path());
    }
    std::sort(ChunkFiles.begin(), ChunkFiles.end());
  }
  (void)world;
}

void UWorldCooperativeSession::ScanSaveChunkCoords(UWorld &world)
{
  SaveChunkCoords.clear();
  SaveChunkIndex = 0;
  SaveUsesTerrainColumns = true;
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
  SaveChunkCoords.reserve(grounds.size());
  for (const glm::ivec3 &ground : grounds)
  {
    SaveChunkCoords.push_back(ground);
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
    GenCenterX = focus_block.x;
    GenCenterZ = focus_block.z;
  }
  else
  {
    GenCenterX = 0;
    GenCenterZ = 0;
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
  const int min_x = GenCenterX - half;
  const int max_x = GenCenterX + half;
  const int min_z = GenCenterZ - half;
  const int max_z = GenCenterZ + half;
  GenPatchMinX = min_x;
  GenPatchMaxX = max_x;
  GenPatchMinZ = min_z;
  GenPatchMaxZ = max_z;

  GenColumnQueue.clear();
  GenColumnQueue.reserve(static_cast<size_t>((max_x - min_x + 1) *
                                             (max_z - min_z + 1)));
  for (int x = min_x; x <= max_x; ++x)
  {
    for (int z = min_z; z <= max_z; ++z)
    {
      GenColumnQueue.push_back({x, z});
    }
  }
  std::sort(GenColumnQueue.begin(), GenColumnQueue.end(),
            [this](const GenColumnEntry &a, const GenColumnEntry &b)
            {
              const int da = (a.X - GenCenterX) * (a.X - GenCenterX) +
                             (a.Z - GenCenterZ) * (a.Z - GenCenterZ);
              const int db = (b.X - GenCenterX) * (b.X - GenCenterX) +
                             (b.Z - GenCenterZ) * (b.Z - GenCenterZ);
              return da < db;
            });

  GenColumnIndex = 0;
  GenTotalColumns = static_cast<int>(GenColumnQueue.size());
  GenDoneColumns = 0;

  GenChunkQueue.clear();
  GenChunkScheduleIndex = 0;
  delete ParallelGen;
  ParallelGen = nullptr;
  const int min_cx = FloorDiv(min_x, CHUNK_SIZE);
  const int max_cx = FloorDiv(max_x, CHUNK_SIZE);
  const int min_cz = FloorDiv(min_z, CHUNK_SIZE);
  const int max_cz = FloorDiv(max_z, CHUNK_SIZE);
  GenChunkQueue.reserve(static_cast<size_t>((max_cx - min_cx + 1) *
                                            (max_cz - min_cz + 1)));
  for (int cx = min_cx; cx <= max_cx; ++cx)
  {
    for (int cz = min_cz; cz <= max_cz; ++cz)
    {
      GenChunkQueue.push_back({cx, cz});
    }
  }
  std::sort(GenChunkQueue.begin(), GenChunkQueue.end(),
            [this](const GenChunkEntry &a, const GenChunkEntry &b)
            {
              const int ax = a.Cx * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int az = a.Cz * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int bx = b.Cx * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int bz = b.Cz * CHUNK_SIZE + CHUNK_SIZE / 2;
              const int da = (ax - GenCenterX) * (ax - GenCenterX) +
                             (az - GenCenterZ) * (az - GenCenterZ);
              const int db = (bx - GenCenterX) * (bx - GenCenterX) +
                             (bz - GenCenterZ) * (bz - GenCenterZ);
              return da < db;
            });
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
    GenDoneColumns += CHUNK_SIZE * CHUNK_SIZE;
    if (ParallelGen->InFlight > 0)
    {
      --ParallelGen->InFlight;
    }
  }

  int scheduled = 0;
  while (GenChunkScheduleIndex < GenChunkQueue.size() &&
         ParallelGen->InFlight < max_inflight &&
         scheduled < budget * 2)
  {
    const GenChunkEntry &entry = GenChunkQueue[GenChunkScheduleIndex++];
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
    request.columnOrigin = glm::ivec2(GenCenterX, GenCenterZ);
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
      GenChunkScheduleIndex >= GenChunkQueue.size() && ParallelGen->InFlight == 0 &&
      ParallelGen->Completed.Empty();
  if (queue_empty)
  {
    if (!ParallelGen->Pool.WaitIdleFor(std::chrono::milliseconds(2000)))
    {
      std::cerr << "CoopGeneration: WaitIdleFor timed out with pending workers"
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
  int processed = 0;
  while (processed < budget && GenColumnIndex < GenColumnQueue.size())
  {
    const GenColumnEntry &entry = GenColumnQueue[GenColumnIndex++];
    world.WorldGen->GenerateColumn(entry.X, entry.Z);
    ++GenDoneColumns;
    ++processed;
  }
  return GenColumnIndex >= GenColumnQueue.size();
}

void UWorldCooperativeSession::SealFluidInGenerationPatch(UWorld &world)
{
  const ProceduralSettings &settings = world.GetProceduralSettings();
  if (!settings.FillWater || !world.BlockRegistry)
  {
    return;
  }
  const int min_cx = FloorDiv(GenPatchMinX, CHUNK_SIZE);
  const int max_cx = FloorDiv(GenPatchMaxX, CHUNK_SIZE);
  const int min_cz = FloorDiv(GenPatchMinZ, CHUNK_SIZE);
  const int max_cz = FloorDiv(GenPatchMaxZ, CHUNK_SIZE);
  for (int cx = min_cx; cx <= max_cx; ++cx)
  {
    for (int cz = min_cz; cz <= max_cz; ++cz)
    {
      (void)SealFluidShoreOnChunkCommitted(
          world.BlockWorld, *world.BlockRegistry, settings,
          world.WorldgenOwnerPackId, glm::ivec3(cx, 0, cz));
    }
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
      SaveDrainIoFrames = 0;
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
                std::to_string(SaveChunkCoords.size()));
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
    break;
  }
  case Phase::Metadata:
  {
    world.LoadWorldData(FolderPath + "/world_data.json");
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
    SpatialStreamingLoad = world.IsStreamingEnabled() && world.HasPersistedSave;
    SpatialRadius = world.RenderDistanceChunks + 1;
    SpatialCenter = ResolveSpatialLoadCenter(world);
    CurrentPhase = Phase::ScanChunks;
    Report(sink, "entities", kPhaseWeightMetadata + kPhaseWeightEntities,
           "Loading entities...");
    break;
  }
  case Phase::ScanChunks:
  {
    ScanChunkFiles(world);
    CurrentPhase = Phase::LoadChunks;
    Report(sink, "scan", kPhaseWeightMetadata + kPhaseWeightEntities,
           "Scanning chunk files...");
    break;
  }
  case Phase::LoadChunks:
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
                 std::to_string(ChunkFiles.size()) + ")...");
    }
    if (ChunkFileIndex >= ChunkFiles.size())
    {
      const size_t blocksInWorld = world.BlockWorld.CountNonAir();
      world.LoadedFromChunkSave = world.HasPersistedSave || ChunkFilesRead > 0 ||
                                  VoxelsFromChunkFiles > 0 || blocksInWorld > 0;
      world.AllowProceduralFill = world.IsStreamingEnabled();
      if (SpatialStreamingLoad && world.LoadedFromChunkSave)
      {
        SpatialDx = -SpatialRadius;
        SpatialDz = -SpatialRadius;
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
    break;
  }
  case Phase::SpatialChunks:
  {
    int loaded = 0;
    const int total = (2 * SpatialRadius + 1) * (2 * SpatialRadius + 1);
    int doneBefore = (SpatialDx + SpatialRadius) * (2 * SpatialRadius + 1) +
                     (SpatialDz + SpatialRadius);
    while (loaded < budget)
    {
      if (SpatialDx > SpatialRadius)
      {
        BeginMeshWarmup(world);
        break;
      }
      if (world.BlockRegistry)
      {
        const glm::ivec3 ground(SpatialCenter.x + SpatialDx, 0,
                                SpatialCenter.z + SpatialDz);
        world.GetChunkStorage().LoadTerrainColumn(
            ground, world.BlockWorld, FolderPath, *world.BlockRegistry,
            world.ProceduralTemplate.MaxHeight);
        if (!IsTerrainChunkComplete(world.BlockWorld, ground,
                                    world.ProceduralTemplate.MaxHeight))
        {
          // Stale/torn column: drop disk + RAM so streaming regenerates.
          ClearTerrainColumnChunks(world.BlockWorld, ground,
                                   world.ProceduralTemplate.MaxHeight);
          world.GetChunkStorage().RemoveTerrainColumnFromDisk(
              FolderPath, ground, world.ProceduralTemplate.MaxHeight);
        }
      }
      ++loaded;
      ++SpatialDz;
      if (SpatialDz > SpatialRadius)
      {
        SpatialDz = -SpatialRadius;
        ++SpatialDx;
      }
    }
    if (CurrentPhase == Phase::SpatialChunks)
    {
      const int doneAfter = (SpatialDx + SpatialRadius) * (2 * SpatialRadius + 1) +
                            (SpatialDz + SpatialRadius);
      const float chunkBase =
          kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks;
      const float frac =
          chunkBase + kPhaseWeightSpatial *
                          (static_cast<float>(doneAfter) /
                           static_cast<float>(std::max(1, total)));
      Report(sink, "spatial", frac, "Loading nearby terrain...");
      (void)doneBefore;
    }
    break;
  }
  case Phase::RelightChunks:
  {
    if (world.BlockRegistry)
    {
      const int relight_budget = std::max(budget * 4, 16);
      int relit = 0;
      while (RelightQueueIndex < RelightQueue.size() &&
             relit < relight_budget)
      {
        world.GetLightingPipeline().RelightChunk(
            world.BlockWorld, *world.BlockRegistry,
            RelightQueue[RelightQueueIndex++], false, true);
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
        relight_base, relight_weight, RelightQueueIndex, RelightQueue.size(), 0,
        0, 0);
    Report(sink, "relight", relight_frac, "Computing lighting...");
    if (RelightQueueIndex >= RelightQueue.size())
    {
      BeginColumnRelightQueue(world);
    }
    break;
  }
  case Phase::RelightBulkChunks:
  {
    const auto tick_t0 = std::chrono::steady_clock::now();
    if (world.BlockRegistry)
    {
      const int chunk_budget = std::clamp(budget * 4, 16, 64);
      int relit = 0;
      while (BulkRelightChunkScheduledIndex < BulkRelightChunkQueue.size() &&
             relit < chunk_budget)
      {
        world.GetLightingPipeline().RelightChunk(
            world.BlockWorld, *world.BlockRegistry,
            BulkRelightChunkQueue[BulkRelightChunkScheduledIndex++], false,
            true);
        ++relit;
      }
    }

    const bool chunks_done =
        BulkRelightChunkScheduledIndex >= BulkRelightChunkQueue.size();

    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const size_t relight_done = BulkRelightChunkScheduledIndex;
    const size_t relight_total = BulkRelightChunkQueue.size();
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
                 std::to_string(relight_total));
    }
    break;
  }
  case Phase::RelightColumns:
  {
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
      while (ColumnRelightScheduledIndex < ColumnRelightQueue.size() &&
             schedule_batch > 0 &&
             world.GetAsyncRelightInFlightCount() < max_inflight)
      {
        const glm::ivec2 &col =
            ColumnRelightQueue[ColumnRelightScheduledIndex++];
        world.EnqueueAsyncTerrainColumnRelight(
            col.x * CHUNK_SIZE, col.y * CHUNK_SIZE, 0, max_y,
            /*include_skylight=*/true, /*include_block_light=*/false,
            /*finalize_pending_gate=*/false);
        --schedule_batch;
      }
      ColumnRelightIndex = ColumnRelightScheduledIndex;
    }
    else if (world.BlockRegistry)
    {
      const int column_budget = std::clamp(budget, 8, 32);
      int relit = 0;
      while (ColumnRelightIndex < ColumnRelightQueue.size() &&
             relit < column_budget)
      {
        const glm::ivec2 &col = ColumnRelightQueue[ColumnRelightIndex++];
        world.GetLightingPipeline().RelightColumn(
            world.BlockWorld, *world.BlockRegistry, col.x, col.y, 0, max_y,
            false, true);
        ++relit;
      }
      ColumnRelightScheduledIndex = ColumnRelightIndex;
    }

    const bool columns_scheduled =
        ColumnRelightScheduledIndex >= ColumnRelightQueue.size();
    const bool async_idle =
        !use_async || !world.HasPendingAsyncRelightWork();
    const bool columns_done = columns_scheduled && async_idle;
    const size_t relight_done = ColumnRelightScheduledIndex;
    const size_t relight_total = ColumnRelightQueue.size();

    bool force_done = columns_done;
    if (!force_done && RelightColumnsStartedAt.time_since_epoch().count() != 0)
    {
      const auto wall_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - RelightColumnsStartedAt)
              .count();
      if (wall_ms >= kRelightColumnsMaxWallMs)
      {
        std::cerr << "RelightColumns: wall timeout after " << wall_ms
                  << "ms, done=" << relight_done << "/" << relight_total
                  << " — continuing to mesh warmup" << std::endl;
        ColumnRelightIndex = ColumnRelightQueue.size();
        ColumnRelightScheduledIndex = ColumnRelightQueue.size();
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
            ? std::min(1.0f, static_cast<float>(ColumnRelightScheduledIndex) /
                                 static_cast<float>(relight_total))
            : 1.0f;
    const float relight_frac = relight_base + relight_weight * relight_inner;
    Report(sink, "relight", relight_frac,
           force_done ? "Lighting ready."
                      : "Computing lighting... " +
                            std::to_string(ColumnRelightScheduledIndex) + "/" +
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
    break;
  }
  case Phase::RelightEmissiveBlockLight:
  {
    if (world.BlockRegistry)
    {
      const int relight_budget = std::max(budget * 4, 16);
      int relit = 0;
      while (EmissiveChunkRelightIndex < EmissiveChunkRelightQueue.size() &&
             relit < relight_budget)
      {
        world.GetLightingPipeline().RelightChunkBlockLight(
            world.BlockWorld, *world.BlockRegistry,
            EmissiveChunkRelightQueue[EmissiveChunkRelightIndex++]);
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
    const size_t emissive_done = EmissiveChunkRelightIndex;
    const size_t emissive_total = EmissiveChunkRelightQueue.size();
    const float relight_frac = ComputeRelightLoadProgress(
        relight_base, relight_weight, emissive_done, emissive_total, 0, 0, 0);
    Report(sink, "relight", relight_frac, "Computing block light...");

    if (EmissiveChunkRelightIndex >= EmissiveChunkRelightQueue.size())
    {
      FinishEmissiveBlockLightRelight(world);
    }
    break;
  }
  case Phase::MeshWarmup:
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
    }
    MeshRebuildTickStats tick_stats;
    for (int pass = 0; pass < pass_limit; ++pass)
    {
      if (!world.BlockRegistry)
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
    if (MeshWarmupTicks == 0)
    {
      MeshWarmupStartedAt = std::chrono::steady_clock::now();
      std::cout << "[WorldLoad] MeshWarmup start: pending="
                << world.MeshService->GetDirtyCount() << " inflight="
                << world.MeshService->GetAsyncInFlightCount()
                << " sync=" << (force_sync_mesh ? "yes" : "no") << std::endl;
    }
    MeshWarmupCompletedTotal += static_cast<size_t>(tick_stats.Completed);
    ++MeshWarmupTicks;
    const bool mesh_done_raw = !world.MeshService->HasPendingDirty() &&
                               !world.MeshService->HasPendingAsyncMeshWork();
    bool mesh_done = mesh_done_raw;
    if (mesh_done && Kind == WorldCoopKind::Create &&
        CooperativeTerrainMeshesIncomplete(world, GenPatchMinX, GenPatchMaxX,
                                           GenPatchMinZ, GenPatchMaxZ))
    {
      const ProceduralSettings &settings = world.GetProceduralSettings();
      const int remesh_min_y = std::max(0, settings.SeaLevel - CHUNK_SIZE);
      const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
      const int min_cx = FloorDiv(GenPatchMinX, CHUNK_SIZE);
      const int max_cx = FloorDiv(GenPatchMaxX, CHUNK_SIZE);
      const int min_cz = FloorDiv(GenPatchMinZ, CHUNK_SIZE);
      const int max_cz = FloorDiv(GenPatchMaxZ, CHUNK_SIZE);
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
    if (MeshWarmupCompletedTotal > MeshWarmupProcessedMax)
    {
      MeshWarmupProcessedMax = MeshWarmupCompletedTotal;
    }
    const float completed_frac =
        MeshWarmupResolvedFraction(MeshWarmupStartPending, pending_now);
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
                     : FormatMeshWarmupProgress(MeshWarmupStartPending,
                                                pending_now));
    if (mesh_done)
    {
      world.SetLightingRelightDeferred(false);
      world.SetLightingSkylightBulkComplete(false);
      if (Kind == WorldCoopKind::Create)
      {
        BeginPrepareEnter();
      }
      else if (MeshWarmupFinalizeOnly)
      {
        // SOTA: FinalizeOnly still owns ColumnFlow until Presentable — do not
        // skip BeginEnterLitGate (Era51 0ms PrepareView / second GpuWarmup).
        if (!world.IsEnterLitGateActive())
        {
          UEnterLitDiagnostics::BeginSession();
          world.BeginEnterLitGate();
        }
        BeginPrepareEnter();
      }
      else
      {
        if (!world.IsEnterLitGateActive())
        {
          UEnterLitDiagnostics::BeginSession();
          world.BeginEnterLitGate();
        }
        CurrentPhase = Phase::PostLoadAnalysis;
      }
    }
    else
    {
      const auto warmup_elapsed_ms =
          MeshWarmupStartedAt.time_since_epoch().count() == 0
              ? 0
              : std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - MeshWarmupStartedAt)
                    .count();
      const bool warmup_wall_timeout =
          warmup_elapsed_ms >= kMeshWarmupMaxWallMs;
      if (MeshWarmupTicks >= kMeshWarmupMaxTicks || warmup_wall_timeout)
      {
        std::cerr << "MeshWarmup: timeout with pending dirty="
                  << world.MeshService->GetDirtyCount()
                  << " ticks=" << MeshWarmupTicks
                  << " wall_ms=" << warmup_elapsed_ms << std::endl;
        world.SetLightingRelightDeferred(false);
        world.SetLightingSkylightBulkComplete(false);
        if (Kind == WorldCoopKind::Create || MeshWarmupFinalizeOnly)
        {
          if (MeshWarmupFinalizeOnly && !world.IsEnterLitGateActive())
          {
            UEnterLitDiagnostics::BeginSession();
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
    break;
  }
  case Phase::PostLoadAnalysis:
  {
    const size_t blocksAfterSpatial = world.BlockWorld.CountNonAir();
    NeedsProceduralFill = false;
    if (blocksAfterSpatial == 0 && !world.LoadedFromChunkSave &&
        world.BlockRegistry)
    {
      NeedsProceduralFill = true;
    }
    else if (blocksAfterSpatial == 0 && world.LoadedFromChunkSave)
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
    break;
  }
  case Phase::ProceduralFill:
  {
    const bool use_parallel = UseParallelChunkGeneration(world);
    const bool generation_done =
        use_parallel ? AdvanceParallelGeneration(world, budget * CHUNK_SIZE)
                     : AdvanceGeneration(world, budget * CHUNK_SIZE);
    if (generation_done)
    {
      if (!ProceduralFillLoadPath || world.Users.empty())
      {
        if (world.WorldGen && world.BlockRegistry)
        {
          world.SpawnPoint = world.WorldGen->ResolvePlayerSpawnPosition(
              world.BlockWorld, *world.BlockRegistry);
        }
        else if (world.WorldGen)
        {
          world.SpawnPoint = world.WorldGen->DefaultSpawnPosition(0, 0);
        }
      }
      CurrentPhase = Phase::FinalizeWorld;
    }
    const float genFrac =
        ProceduralFillProgressBase() +
        kProceduralFillWeightGenerate *
            (static_cast<float>(GenDoneColumns) /
             static_cast<float>(std::max(1, GenTotalColumns)));
    Report(sink, "generate", genFrac, "Generating terrain...");
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
      MeshWarmupFinalizeOnly = true;
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
    if (Kind == WorldCoopKind::Create)
    {
      if (StreamingWarmupTicks == 0)
      {
        WarnIfTerrainMeshesMissing(world, "PrepareView before spawn warmup");
        StreamingWarmupWallStart = std::chrono::steady_clock::now();
        StreamingWarmupPeakDebt = 0;
        StreamingWarmupLastRawDebt = 0;
        StreamingWarmupDisplayDebt = 0;
        StreamingWarmupLitWarnLogged = false;
        StreamingWarmupBestFovDebt = INT_MAX;
        StreamingWarmupLitProgressAt = StreamingWarmupWallStart;
        StreamingWarmupLitStallLogged = false;
        if (!world.IsEnterLitGateActive())
        {
          UEnterLitDiagnostics::BeginSession();
          world.BeginEnterLitGate();
        }
      }
      TickCreateSpawnMeshWarmup(world, std::max(1, budget / 2));
      // Era41: light LitDrawable FOV on create bar (async Capture + workers).
      world.TickEnterFovLitPass(
          std::max(1, URuntimeTuning::Get().EnterFovLitCaptureBudget));
      ++StreamingWarmupTicks;
      bool underfeet_lit = false;
      const int raw_debt = world.CountCreateNearFovWarmupDebt(&underfeet_lit);
      const int fov_debt = world.CountEnterFovLitDebt();
      if (fov_debt < StreamingWarmupBestFovDebt)
      {
        StreamingWarmupBestFovDebt = fov_debt;
        StreamingWarmupLitProgressAt = std::chrono::steady_clock::now();
      }
      const auto &phys = world.GetPhysicsTelemetry();
      const int debt = raw_debt + fov_debt + phys.FocusDarkMesh +
                       phys.SoftDeferEmptyPlaceholderN;
      if (debt > StreamingWarmupPeakDebt)
      {
        StreamingWarmupPeakDebt = debt;
      }
      // Era35 P3: monotonic display debt — only decreases, never jumps up.
      if (StreamingWarmupTicks == 1)
      {
        StreamingWarmupDisplayDebt = debt;
      }
      else
      {
        StreamingWarmupDisplayDebt = std::min(StreamingWarmupDisplayDebt, debt);
      }
      StreamingWarmupLastRawDebt = debt;
      const bool spawn_settled = IsCreateSpawnWarmupSettled(world);
      const float prepare_view_base =
          CooperativeCreateMeshProgressBase() + kCreateWeightMeshWarmup;
      const int denom = std::max(1, StreamingWarmupPeakDebt);
      const float stream_inner =
          spawn_settled ? 1.0f
                        : (1.0f - CreateBarDebtFraction(
                                      StreamingWarmupDisplayDebt, denom));
      const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    StreamingWarmupWallStart)
                                    .count();
      const double ms_since_lit_progress =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - StreamingWarmupLitProgressAt)
              .count();
      const bool lit_progress_stalled = EnterLitDebtProgressStalled(
          fov_debt, StreamingWarmupBestFovDebt, underfeet_lit,
          ms_since_lit_progress,
          static_cast<double>(EnterLitProgressStallMs()), elapsed_ms,
          static_cast<double>(CreateSpawnWarmupSoftWallMs()));
      if (lit_progress_stalled && !StreamingWarmupLitStallLogged)
      {
        StreamingWarmupLitStallLogged = true;
        std::cerr << "[LitRing] create lit progress-stall abort (" << elapsed_ms
                  << "ms, lit=" << fov_debt
                  << ", best=" << StreamingWarmupBestFovDebt << ")\n";
      }
      EnterLitSample lit_sample{};
      UEnterLitDiagnostics::Sample(world, elapsed_ms, lit_sample);
      UEnterLitDiagnostics::MaybeLog(lit_sample, StreamingWarmupTicks);
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
                 std::to_string(StreamingWarmupDisplayDebt) + " left";
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
          if (!StreamingWarmupLitWarnLogged &&
              elapsed_ms >=
                  static_cast<double>(
                      URuntimeTuning::Get().EnterFovLitHardWallMs))
          {
            StreamingWarmupLitWarnLogged = true;
            std::cerr << "[Era42] create lit still draining past warn wall ("
                      << elapsed_ms << "ms, lit=" << fov_debt << ")\n";
          }
          break;
        }
      }
      else if (ShouldSoftLeaveCreateSpawnWarmup(underfeet_lit, elapsed_ms))
      {
        std::cerr << "[Era34] create spawn soft-wall after underfeet lit ("
                  << elapsed_ms << "ms, debt=" << debt << ")\n";
      }
      else if (ShouldHardLeaveCreateSpawnWarmup(elapsed_ms,
                                                StreamingWarmupTicks))
      {
        std::cerr << "[Era34] create spawn hard ceiling (" << elapsed_ms
                  << "ms, ticks=" << StreamingWarmupTicks << ", debt=" << debt
                  << ")\n";
      }
      else
      {
        break;
      }
    }
    else
    {
      WarnIfTerrainMeshesMissing(world, "PrepareView before warmup");
      if (world.IsEnterLitGateActive())
      {
        if (StreamingWarmupTicks == 0)
        {
          StreamingWarmupWallStart = std::chrono::steady_clock::now();
          StreamingWarmupPeakDebt = 0;
          StreamingWarmupLastRawDebt = 0;
          StreamingWarmupDisplayDebt = 0;
          StreamingWarmupLitWarnLogged = false;
          StreamingWarmupAbortDrainMode = false;
          StreamingWarmupAbortLogged = false;
          StreamingWarmupAbortCapLogged = false;
          StreamingWarmupBestFovDebt = INT_MAX;
          StreamingWarmupLitProgressAt = StreamingWarmupWallStart;
          StreamingWarmupLitStallLogged = false;
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
        ++StreamingWarmupTicks;
        const int fov_debt = world.CountEnterFovLitDebt();
        if (fov_debt < StreamingWarmupBestFovDebt)
        {
          StreamingWarmupBestFovDebt = fov_debt;
          StreamingWarmupLitProgressAt = std::chrono::steady_clock::now();
        }
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() -
                                      StreamingWarmupWallStart)
                                      .count();
        const double ms_since_lit_progress =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - StreamingWarmupLitProgressAt)
                .count();
        EnterLitSample lit_sample{};
        UEnterLitDiagnostics::Sample(world, elapsed_ms, lit_sample);
        UEnterLitDiagnostics::MaybeLog(lit_sample, StreamingWarmupTicks);
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
        if (combined_debt > StreamingWarmupPeakDebt)
        {
          StreamingWarmupPeakDebt = combined_debt;
        }
        if (StreamingWarmupTicks == 1)
        {
          StreamingWarmupDisplayDebt = combined_debt;
        }
        else
        {
          StreamingWarmupDisplayDebt =
              std::min(StreamingWarmupDisplayDebt, combined_debt);
        }
        StreamingWarmupLastRawDebt = combined_debt;
        const float prepare_view_base =
            ProceduralFillLoadPath
                ? (ProceduralFillMeshProgressBase() +
                   kProceduralFillWeightMeshWarmup)
                : CooperativeLoadProgressAfterMesh();
        const int denom = std::max(1, StreamingWarmupPeakDebt);
        const bool ring_ready = world.IsSpawnMeshRingReady();
        const bool visibility_ready = world.IsEnterVisibilityReady();
        const int visibility_debt = lit_sample.visibility_debt;
        const bool mesh_blockers_clear = !world.NeedsEnterGameMeshWarmup();
        const bool underfeet_present = world.IsEnterUnderfeetPresentReady();
        const bool lit_progress_stalled = EnterLitDebtProgressStalled(
            fov_debt, StreamingWarmupBestFovDebt, underfeet_present,
            ms_since_lit_progress,
            static_cast<double>(EnterLitProgressStallMs()), elapsed_ms,
            static_cast<double>(CreateSpawnWarmupSoftWallMs()));
        if (lit_progress_stalled && !StreamingWarmupLitStallLogged)
        {
          StreamingWarmupLitStallLogged = true;
          LOG(WARNING) << "[LitRing] load lit progress-stall abort elapsed_ms="
                       << elapsed_ms << " lit=" << fov_debt
                       << " best=" << StreamingWarmupBestFovDebt;
          CubatariumFlushLogs();
        }
        if (lit_progress_stalled)
        {
          StreamingWarmupAbortDrainMode = true;
        }
        const glm::ivec3 underfeet_center =
            UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
        const int underfeet_gpu_pending =
            world.GetMeshService().CountPendingGpuAppliesInHorizontalRadius(
                underfeet_center, 1);
        const bool abort_underfeet_cap = ShouldReleaseEnterAfterAbortUnderfeetCap(
            StreamingWarmupAbortDrainMode, elapsed_ms,
            URuntimeTuning::Get().EnterForceInGameMs, underfeet_present,
            underfeet_gpu_pending);
        // LitRing C: stall with underfeet → settle with FOV holes OK (finite load).
        const bool load_settled =
            (ring_ready && visibility_ready && mesh_blockers_clear) ||
            (lit_progress_stalled && underfeet_present &&
             underfeet_gpu_pending <= 0) ||
            (abort_underfeet_cap && underfeet_present &&
             underfeet_gpu_pending <= 0 &&
             (fov_debt <= 0 || lit_progress_stalled));
        if ((abort_underfeet_cap || lit_progress_stalled) && load_settled &&
            !StreamingWarmupAbortCapLogged)
        {
          StreamingWarmupAbortCapLogged = true;
          LOG(WARNING) << "[EnterWarmup] coop_abort_underfeet_cap elapsed_ms="
                       << elapsed_ms << " ring_ready=" << (ring_ready ? 1 : 0)
                       << " visibility_debt=" << visibility_debt
                       << " lit_stall=" << (lit_progress_stalled ? 1 : 0);
          CubatariumFlushLogs();
        }
        if (!StreamingWarmupAbortDrainMode &&
            ShouldForceEnterMeshAbort(fov_debt, ring_ready, elapsed_ms,
                                      URuntimeTuning::Get().EnterMeshAbortMs))
        {
          StreamingWarmupAbortDrainMode = true;
          if (!StreamingWarmupAbortLogged)
          {
            StreamingWarmupAbortLogged = true;
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
        if (ShouldEscalateEnterWarmupGpuDrain(StreamingWarmupAbortDrainMode,
                                              elapsed_ms) &&
            world.NeedsEnterGameMeshWarmup() &&
            !world.IsEnterLitGateActive())
        {
          world.DrainEnterGameMeshWarmup(kCoopEnterMeshBudget * 2);
        }
        const float stream_inner =
            load_settled
                ? 1.0f
                : (1.0f - CreateBarDebtFraction(StreamingWarmupDisplayDebt,
                                                denom));
        const std::string status = BuildEnterWarmupStatus(
            lit_sample, fov_debt, ring_ready, StreamingWarmupAbortDrainMode,
            elapsed_ms, URuntimeTuning::Get().EnterFovLitHardWallMs,
            visibility_debt);
        Report(sink, "prepare_view",
               prepare_view_base + kPhaseWeightPrepareView * stream_inner,
               status);
        if (!load_settled)
        {
          break;
        }
        UEnterLitDiagnostics::MaybeLogProfileSummary(lit_sample);
      }
    }
    world.WarmupVisibleListAtCamera();
    WarnIfTerrainMeshesMissing(world, "PrepareView after warmup");
    // Mark before EndEnterLitGate — vis-ready uses the enter snapshot while
    // the gate is still active. Consume happens after GpuWarmup.
    FinalizeCooperativeLoadForEnterGame(world, Kind);
    if (world.IsEnterLitGateActive())
    {
      world.EndEnterLitGate();
      UEnterLitDiagnostics::EndSession();
    }
    CurrentPhase = Phase::Done;
    Active = false;
    StreamingWarmupTicks = 0;
    StreamingWarmupPeakDebt = 0;
    StreamingWarmupLastRawDebt = 0;
    StreamingWarmupDisplayDebt = 0;
    StreamingWarmupLitWarnLogged = false;
    StreamingWarmupAbortDrainMode = false;
    StreamingWarmupAbortLogged = false;
    StreamingWarmupBestFovDebt = INT_MAX;
    StreamingWarmupLitStallLogged = false;
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
    break;
  }
  case Phase::DrainAsyncIo:
  {
    // First frame: hard-cancel leftover gen + stale pending IO maps so drain
    // cannot wait forever on cancelled jobs.
    if (SaveDrainIoFrames == 0)
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
    ++SaveDrainIoFrames;
    const int max_drain_frames =
        world.BackgroundQuiesceFinished ? 4 : 24;
    const float frac = std::min(
        0.04f, 0.04f * static_cast<float>(SaveDrainIoFrames) /
                    static_cast<float>(std::max(1, max_drain_frames)));
    Report(sink, "init", frac, "Flushing pending terrain IO...");
    if (drained || SaveDrainIoFrames >= max_drain_frames)
    {
      if (!drained && world.Persistence)
      {
        (void)world.Persistence->AbortAsyncChunkIoFor(
            std::chrono::milliseconds(0));
      }
      CubatariumLogInfo(
          "Save",
          std::string("drain_async_io end frames=") +
              std::to_string(SaveDrainIoFrames) +
              " drained=" + (drained ? "1" : "0"));
      CurrentPhase = Phase::ScanSaveChunks;
    }
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
            std::to_string(SaveChunkCoords.size()));
    break;
  }
  case Phase::SaveChunks:
  {
    if (!world.BlockRegistry)
    {
      CurrentPhase = Phase::SaveMetadata;
      break;
    }
    const bool save_by_ground_column = SaveUsesTerrainColumns;
    int saved = 0;
    while (SaveChunkIndex < SaveChunkCoords.size() && saved < budget)
    {
      const glm::ivec3 coord = SaveChunkCoords[SaveChunkIndex++];
      if (save_by_ground_column)
      {
        if (IsTerrainChunkComplete(world.BlockWorld, coord,
                                   world.ProceduralTemplate.MaxHeight))
        {
          world.GetChunkStorage().SaveTerrainColumn(
              coord, world.BlockWorld, FolderPath, *world.BlockRegistry,
              world.ProceduralTemplate.MaxHeight);
        }
        else
        {
          world.GetChunkStorage().RemoveTerrainColumnFromDisk(
              FolderPath, coord, world.ProceduralTemplate.MaxHeight);
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
        SaveChunkCoords.empty()
            ? 0.85f
            : 0.05f + 0.8f * (static_cast<float>(SaveChunkIndex) /
                               static_cast<float>(SaveChunkCoords.size()));
    Report(sink, "chunks", frac,
           "Saving chunks (" + std::to_string(SaveChunkIndex) + "/" +
               std::to_string(SaveChunkCoords.size()) + ")...");
    if (SaveChunkIndex >= SaveChunkCoords.size())
    {
      CurrentPhase = Phase::SaveMetadata;
    }
    break;
  }
  case Phase::SaveMetadata:
  {
    CubatariumLogInfo("Save", "save_metadata begin");
    world.GetChunkStorage().WriteStorageMarker(FolderPath);
    world.SaveUsers(FolderPath + "/users.json");
    world.SaveCreatures(FolderPath + "/creatures.json");
    world.SaveWorldData(FolderPath + "/world_data.json");
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
    const bool use_parallel = UseParallelChunkGeneration(world);
    const bool generation_done =
        use_parallel ? AdvanceParallelGeneration(world, budget * CHUNK_SIZE)
                     : AdvanceGeneration(world, budget * CHUNK_SIZE);
    if (generation_done)
    {
      if (world.WorldGen)
      {
        if (world.BlockRegistry)
        {
          world.SpawnPoint = world.WorldGen->ResolvePlayerSpawnPosition(
              world.BlockWorld, *world.BlockRegistry);
        }
        else
        {
          world.SpawnPoint = world.WorldGen->DefaultSpawnPosition(0, 0);
        }
      }
      CurrentPhase = Phase::PostCreate;
    }
    const float frac =
        kCreateWeightInit +
        kCreateWeightGenerate *
            (static_cast<float>(GenDoneColumns) /
             static_cast<float>(std::max(1, GenTotalColumns)));
    Report(sink, "generate", frac, "Generating terrain...");
    break;
  }
  case Phase::PostCreate:
  {
    SealFluidInGenerationPatch(world);
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
    BeginMeshWarmup(world);
    if (CurrentPhase == Phase::MeshWarmup)
    {
      Report(sink, "mesh_warmup", CooperativeCreateMeshProgressBase(),
             "Building terrain meshes...");
    }
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

  return CurrentPhase == Phase::Done;
}

} // namespace cutum
