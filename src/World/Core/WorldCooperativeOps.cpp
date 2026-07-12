#include "World/Core/WorldCooperativeOps.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/WorldStreaming.h"
#include "Core/Jobs/JobThreadPool.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"
#include "World/Persistence/WorldPersistence.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <unordered_set>

namespace cutum
{

struct CooperativeParallelGenState
{
  CooperativeParallelGenState()
      : Pool(std::max<std::size_t>(2, std::thread::hardware_concurrency()))
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
constexpr int kCreateSpawnWarmupMaxTicks = 48;
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

bool CooperativeTerrainMeshesIncomplete(const UWorld &world)
{
  size_t ground_chunks = 0;
  size_t meshed_chunks = 0;
  world.GetBlockWorld().GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        if (chunk.GetCoord().y != 0)
        {
          return;
        }
        ++ground_chunks;
        if (world.GetMeshService().HasGreedyMesh(chunk.GetCoord()))
        {
          ++meshed_chunks;
        }
      });
  return ground_chunks > 0 && meshed_chunks < ground_chunks;
}

bool IsCreateSpawnWarmupSettled(const UWorld &world)
{
  return world.IsCreateSpawnWarmupSettled();
}

void TickCreateSpawnMeshWarmup(UWorld &world, int budget)
{
  world.DrainSpawnRadiusMeshWarmup(budget);
  world.TickEnterStreamingWarmup(std::max(1, budget / 2));
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
  if (!world.GetMeshService().HasPendingDirty() &&
      !world.GetMeshService().HasPendingAsyncMeshWork())
  {
    world.MarkSpawnAreaPreparedByCooperativeLoad();
  }
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

  // Bulk create/load already ran column skylight relight; a full-world emissive
  // scan blocks the main thread for a long time on large saves.
  if (Kind == WorldCoopKind::Create || Kind == WorldCoopKind::Load)
  {
    std::cout << "[WorldLoad] RelightEmissiveBlockLight: skipped (coop "
              << (Kind == WorldCoopKind::Create ? "create" : "load")
              << "), starting mesh warmup" << std::endl;
    world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
    BeginMeshWarmupInner(world);
    return;
  }

  if (world.BlockRegistry)
  {
    world.BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        {
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
  world.BlockCounter.MarkNeedsRecount();
  world.MeshService->CancelAsyncInFlightKeepDirty();
  bool has_chunks = false;
  world.BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &) { has_chunks = true; });
  if (has_chunks)
  {
    world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
  }
  MeshWarmupTicks = 0;
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

void UWorldCooperativeSession::Cancel()
{
  Active = false;
  CurrentPhase = Phase::Done;
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
                                         const std::string &world_folder_path)
{
  delete ParallelGen;
  *this = UWorldCooperativeSession{};
  ParallelGen = nullptr;
  Kind = WorldCoopKind::Save;
  Active = true;
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
    grounds.reserve(world.ModifiedChunks.size());
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

void UWorldCooperativeSession::InitGenerationGrid(UWorld &world)
{
  if (!world.WorldGen)
  {
    world.RebuildWorldGenPipeline();
  }
  else if (world.ObjectLibrary && world.BlockRegistry)
  {
    world.ObjectLibrary->RebindBlockIds(*world.BlockRegistry);
  }
  GenCenterX = 0;
  GenCenterZ = 0;
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
      std::max<std::size_t>(2, std::thread::hardware_concurrency()) * 2;

  for (ChunkPopulateResult &result :
       ParallelGen->Completed.DrainUpTo(static_cast<std::size_t>(budget) * 2))
  {
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
    request.token.coord = request.chunkCoord;
    request.token.sequence = 1;
    request.columnOrigin = glm::ivec2(GenCenterX, GenCenterZ);
    request.hasColumnOrigin = true;
    request.objects = world.ObjectLibrary;
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
    ParallelGen->Pool.WaitIdle();
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
      world.QuiesceBackgroundWork(std::chrono::milliseconds(2000));
      world.RefreshBlockRegistry();
      std::filesystem::create_directories(FolderPath);
      world.SetWorldFolderPath(FolderPath);
      std::filesystem::create_directories(FolderPath + "/chunks");
      SaveDrainIoFrames = 0;
      CurrentPhase = Phase::DrainAsyncIo;
      Report(sink, "init", 0.f, "Preparing save...");
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
        world.GetChunkStorage().LoadTerrainColumn(
            glm::ivec3(SpatialCenter.x + SpatialDx, 0,
                       SpatialCenter.z + SpatialDz),
            world.BlockWorld, FolderPath, *world.BlockRegistry,
            world.ProceduralTemplate.MaxHeight);
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
        RelightChunk(world.BlockWorld, *world.BlockRegistry,
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
        RelightChunk(world.BlockWorld, *world.BlockRegistry,
                     BulkRelightChunkQueue[BulkRelightChunkScheduledIndex++],
                     false, true);
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
      ReportMeshWarmupStart(sink);
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
    if (world.BlockRegistry)
    {
      const int column_budget = std::clamp(budget, 8, 32);
      int relit = 0;
      while (ColumnRelightIndex < ColumnRelightQueue.size() &&
             relit < column_budget)
      {
        const glm::ivec2 &col = ColumnRelightQueue[ColumnRelightIndex++];
        RelightColumn(world.BlockWorld, *world.BlockRegistry, col.x, col.y, 0,
                      max_y, false, true);
        ++relit;
      }
    }

    const bool columns_done =
        ColumnRelightIndex >= ColumnRelightQueue.size();
    const size_t relight_done = ColumnRelightIndex;
    const size_t relight_total = ColumnRelightQueue.size();

    const float relight_base =
        Kind == WorldCoopKind::Create
            ? (kCreateWeightInit + kCreateWeightGenerate)
            : (ProceduralFillLoadPath ? ProceduralFillProgressAfterGenerate()
                                      : CooperativeLoadProgressBase());
    const float relight_weight =
        Kind == WorldCoopKind::Create ? kCreateWeightRelight : kPhaseWeightRelight;
    const float relight_inner =
        relight_total > 0
            ? std::min(1.0f, static_cast<float>(relight_done) /
                                 static_cast<float>(relight_total))
            : 1.0f;
    const float relight_frac = relight_base + relight_weight * relight_inner;
    Report(sink, "relight", relight_frac,
           columns_done ? "Lighting ready."
                       : "Computing lighting... " +
                             std::to_string(relight_done) + "/" +
                             std::to_string(relight_total));

    if (columns_done)
    {
      const auto tick_t1 = std::chrono::steady_clock::now();
      const double tick_ms =
          std::chrono::duration<double, std::milli>(tick_t1 - tick_t0).count();
      std::cout << "[WorldLoad] RelightColumns done: " << relight_total
                << " columns, last_tick_ms=" << tick_ms << std::endl;
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
        RelightChunkBlockLight(world.BlockWorld, *world.BlockRegistry,
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
    const bool force_sync_mesh = Kind != WorldCoopKind::Create;
    const UChunkEmergeCoordinator::FrameBudget mesh_budget =
        create_mesh_warmup
            ? UChunkEmergeCoordinator::CreateMeshWarmupBudget(budget)
            : UChunkEmergeCoordinator::CooperativeWarmupBudget(budget);
    const int pass_limit = create_mesh_warmup ? 8 : 1;
    const int sync_mesh_budget =
        force_sync_mesh ? std::max(8, budget * 4) : mesh_budget.MaxMeshDrain;
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
        CooperativeTerrainMeshesIncomplete(world))
    {
      world.MeshService->MarkAllDirtyFromWorld(world.BlockWorld);
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
        MeshWarmupStartPending > 0
            ? std::min(1.0f,
                       static_cast<float>(MeshWarmupCompletedTotal) /
                           static_cast<float>(MeshWarmupStartPending))
            : 1.0f;
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
    const size_t done_count = MeshWarmupCompletedTotal;
    const size_t total_count = MeshWarmupStartPending;
    Report(sink, "mesh_warmup", mesh_frac,
           mesh_done ? "Terrain meshes ready."
                     : "Building meshes... " + std::to_string(done_count) +
                           "/" + std::to_string(total_count) +
                           " (" + std::to_string(pending_now) + " pending)");
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
        BeginPrepareEnter();
      }
      else
      {
        CurrentPhase = Phase::PostLoadAnalysis;
      }
    }
    else if (MeshWarmupTicks >= kMeshWarmupMaxTicks)
    {
      std::cerr << "MeshWarmup: timeout with pending dirty="
                << world.MeshService->GetDirtyCount() << std::endl;
      world.SetLightingRelightDeferred(false);
      world.SetLightingSkylightBulkComplete(false);
      if (Kind == WorldCoopKind::Create || MeshWarmupFinalizeOnly)
      {
        BeginPrepareEnter();
      }
      else
      {
        CurrentPhase = Phase::PostLoadAnalysis;
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
        world.HasPersistedSave = false;
        world.LoadedFromChunkSave = false;
        world.AllowProceduralFill = true;
        NeedsProceduralFill = true;
      }
    }
    if (NeedsProceduralFill)
    {
      ProceduralFillLoadPath = true;
      InitGenerationGrid(world);
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
      if (world.WorldGen && world.BlockRegistry)
      {
        world.SpawnPoint = world.WorldGen->ResolvePlayerSpawnPosition(
            world.BlockWorld, *world.BlockRegistry);
      }
      else if (world.WorldGen)
      {
        world.SpawnPoint = world.WorldGen->DefaultSpawnPosition(0, 0);
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
      }
      TickCreateSpawnMeshWarmup(world, std::max(1, budget / 2));
      ++StreamingWarmupTicks;
      const bool spawn_settled = IsCreateSpawnWarmupSettled(world);
      const float prepare_view_base =
          CooperativeCreateMeshProgressBase() + kCreateWeightMeshWarmup;
      const float stream_inner =
          spawn_settled ? 1.0f
                        : std::min(0.95f,
                                   static_cast<float>(StreamingWarmupTicks) /
                                       static_cast<float>(
                                           kCreateSpawnWarmupMaxTicks));
      Report(sink, "prepare_view",
             prepare_view_base + kCreateWeightPrepare * stream_inner,
             spawn_settled ? "Preparing view..."
                           : "Loading nearby terrain...");
      if (!spawn_settled && StreamingWarmupTicks < kCreateSpawnWarmupMaxTicks)
      {
        break;
      }
    }
    else
    {
      WarnIfTerrainMeshesMissing(world, "PrepareView before warmup");
    }
    world.WarmupVisibleListAtCamera();
    WarnIfTerrainMeshesMissing(world, "PrepareView after warmup");
    FinalizeCooperativeLoadForEnterGame(world, Kind);
    CurrentPhase = Phase::Done;
    Active = false;
    StreamingWarmupTicks = 0;
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
    bool drained = true;
    if (world.Persistence)
    {
      drained = world.Persistence->TickDrainAsyncChunkIo(
          world, std::max(budget * 2, 8));
    }
    ++SaveDrainIoFrames;
    const float frac = std::min(0.04f, 0.04f * static_cast<float>(SaveDrainIoFrames) /
                                           64.0f);
    Report(sink, "init", frac, "Flushing pending terrain IO...");
    if (drained)
    {
      CurrentPhase = Phase::ScanSaveChunks;
    }
    else if (SaveDrainIoFrames >= 512)
    {
      if (world.Persistence)
      {
        (void)world.Persistence->AbortAsyncChunkIoFor(
            std::chrono::milliseconds(100));
      }
      CurrentPhase = Phase::ScanSaveChunks;
    }
    break;
  }
  case Phase::ScanSaveChunks:
  {
    ScanSaveChunkCoords(world);
    CurrentPhase = Phase::SaveChunks;
    Report(sink, "scan", 0.02f, "Collecting chunks...");
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
    world.GetChunkStorage().WriteStorageMarker(FolderPath);
    world.SaveUsers(FolderPath + "/users.json");
    world.SaveCreatures(FolderPath + "/creatures.json");
    world.SaveWorldData(FolderPath + "/world_data.json");
    world.SaveMovementDiagnostics(FolderPath + "/movement_diagnostics.json");
    world.ModifiedChunks.clear();
    if (world.Streaming)
    {
      world.Streaming->ResumeStreamerAfterQuiesce();
    }
    world.AllowProceduralFill = world.IsStreamingEnabled();
    CurrentPhase = Phase::Done;
    Active = false;
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
