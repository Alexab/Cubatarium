#include "World/Core/WorldCooperativeOps.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace cutum
{

namespace
{

constexpr float kPhaseWeightMetadata = 0.05f;
constexpr float kPhaseWeightEntities = 0.05f;
constexpr float kPhaseWeightChunks = 0.50f;
constexpr float kPhaseWeightSpatial = 0.12f;
constexpr float kPhaseWeightMeshWarmup = 0.20f;
constexpr float kPhaseWeightGenerate = 0.12f;
constexpr int kMeshWarmupMaxTicks = 50000;

} // namespace

void UWorldCooperativeSession::BeginMeshWarmup(UWorld &world)
{
  if (world.IsLightingRelightDeferred() && world.BlockRegistry)
  {
    RelightAllLoadedChunks(world.BlockWorld, *world.BlockRegistry);
    world.SetLightingRelightDeferred(false);
  }
  world.BlockCounter.MarkNeedsRecount();
  if (world.MeshService->GetDirtyCount() == 0 &&
      !world.MeshService->HasPendingAsyncMeshWork())
  {
    world.BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        { world.MeshService->MarkDirty(chunk.GetCoord()); });
  }
  MeshWarmupTicks = 0;
  MeshWarmupStartPending =
      world.MeshService->GetDirtyCount() +
      static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
  if (MeshWarmupStartPending == 0)
  {
    MeshWarmupStartPending = 1;
  }
  CurrentPhase = Phase::MeshWarmup;
  MeshWarmupFinalizeOnly = false;
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
  *this = UWorldCooperativeSession{};
  Kind = WorldCoopKind::Load;
  Active = true;
  FolderPath = world_folder_path;
  CurrentPhase = Phase::Init;
  (void)world;
}

void UWorldCooperativeSession::BeginSave(UWorld &world,
                                         const std::string &world_folder_path)
{
  *this = UWorldCooperativeSession{};
  Kind = WorldCoopKind::Save;
  Active = true;
  FolderPath = world_folder_path;
  CurrentPhase = Phase::Init;
  (void)world;
}

void UWorldCooperativeSession::BeginCreate(UWorld &world,
                                           const std::string &world_name)
{
  *this = UWorldCooperativeSession{};
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
  if (world.ModifiedChunks.empty())
  {
    world.BlockWorld.GetChunkManager().ForEachChunk(
        [&](const UChunk &chunk)
        { SaveChunkCoords.push_back(chunk.GetCoord()); });
  }
  else
  {
    SaveChunkCoords.assign(world.ModifiedChunks.begin(),
                           world.ModifiedChunks.end());
  }
}

void UWorldCooperativeSession::InitGenerationGrid(UWorld &world)
{
  if (!world.WorldGen)
  {
    world.RebuildWorldGenPipeline();
  }
  GenCenterX = 0;
  GenCenterZ = 0;
  const int patch_radius_blocks =
      std::max(1, world.RenderDistanceChunks) * CHUNK_SIZE;
  const int half = patch_radius_blocks;
  const int min_x = GenCenterX - half;
  const int max_x = GenCenterX + half;
  const int min_z = GenCenterZ - half;
  const int max_z = GenCenterZ + half;

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
      world.SetLightingRelightDeferred(true);
      world.BlockWorldReady = false;
      world.LoadedFromChunkSave = false;
      world.BlockWorld.Clear();
      world.MeshService->GetCache().MarkAllDirty();
      world.ResetPhysicsRuntimeState();
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
      world.RefreshBlockRegistry();
      std::filesystem::create_directories(FolderPath);
      world.SetWorldFolderPath(FolderPath);
      std::filesystem::create_directories(FolderPath + "/chunks");
      CurrentPhase = Phase::ScanSaveChunks;
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
      world.BlockWorld.Clear();
      world.ModifiedChunks.clear();
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
    const glm::ivec3 spawnBlock = WorldPosToBlock(world.SpawnPoint);
    SpatialCenter = UChunkManager::WorldToChunk(spawnBlock);
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
        CurrentPhase = Phase::PostLoadAnalysis;
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
  case Phase::MeshWarmup:
  {
    if (world.BlockRegistry)
    {
      const UChunkEmergeCoordinator::FrameBudget mesh_budget =
          UChunkEmergeCoordinator::CooperativeWarmupBudget(budget);
      world.MeshService->RebuildDirtyChunks(world.BlockWorld, *world.BlockRegistry,
                                            mesh_budget.MaxMeshDrain,
                                            mesh_budget.MaxMeshSchedule);
      world.MeshService->DrainAsyncMeshResults(world.BlockWorld,
                                               *world.BlockRegistry,
                                               mesh_budget.MaxMeshDrain);
    }
    ++MeshWarmupTicks;
    const bool mesh_done = !world.MeshService->HasPendingDirty() &&
                           !world.MeshService->HasPendingAsyncMeshWork();
    const size_t pending_now =
        world.MeshService->GetDirtyCount() +
        static_cast<size_t>(world.MeshService->GetAsyncInFlightCount());
    const float mesh_inner =
        mesh_done ? 1.0f
                  : 1.0f - static_cast<float>(pending_now) /
                                static_cast<float>(MeshWarmupStartPending);
    const float mesh_base =
        Kind == WorldCoopKind::Create
            ? (0.05f + 0.75f)
            : (kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks +
               kPhaseWeightSpatial);
    const float mesh_frac =
        mesh_base + kPhaseWeightMeshWarmup * std::clamp(mesh_inner, 0.0f, 1.0f);
    Report(sink, "mesh_warmup", mesh_frac,
           mesh_done ? "Terrain meshes ready."
                     : "Building terrain meshes...");
    if (mesh_done)
    {
      if (Kind == WorldCoopKind::Create)
      {
        world.FinalizePlayerAfterWorldLoad();
        CurrentPhase = Phase::Done;
        Active = false;
        Report(sink, "done", 1.f, "World created.");
      }
      else if (MeshWarmupFinalizeOnly)
      {
        world.FinalizePlayerAfterWorldLoad();
        CurrentPhase = Phase::Done;
        Active = false;
        Report(sink, "done", 1.f, "World loaded.");
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
      if (Kind == WorldCoopKind::Create || MeshWarmupFinalizeOnly)
      {
        world.FinalizePlayerAfterWorldLoad();
        CurrentPhase = Phase::Done;
        Active = false;
        Report(sink, "done", 1.f,
               Kind == WorldCoopKind::Create ? "World created." : "World loaded.");
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
      InitGenerationGrid(world);
      CurrentPhase = Phase::ProceduralFill;
      Report(sink, "generate", 0.72f, "Generating terrain...");
    }
    else
    {
      CurrentPhase = Phase::FinalizeWorld;
    }
    break;
  }
  case Phase::ProceduralFill:
  {
    if (AdvanceGeneration(world, budget * CHUNK_SIZE))
    {
      world.SpawnPoint = world.WorldGen
                             ? world.WorldGen->DefaultSpawnPosition(0, 0)
                             : world.SpawnPoint;
      CurrentPhase = Phase::FinalizeWorld;
    }
    const float genFrac =
        0.72f + 0.18f * (static_cast<float>(GenDoneColumns) /
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
             kPhaseWeightMetadata + kPhaseWeightEntities + kPhaseWeightChunks,
             "Building terrain meshes...");
      break;
    }
    world.FinalizePlayerAfterWorldLoad();
    CurrentPhase = Phase::Done;
    Active = false;
    Report(sink, "done", 1.f, "World loaded.");
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
    int saved = 0;
    while (SaveChunkIndex < SaveChunkCoords.size() && saved < budget)
    {
      const glm::ivec3 coord = SaveChunkCoords[SaveChunkIndex++];
      const UChunk *chunk = world.BlockWorld.GetChunkManager().GetChunk(coord);
      if (chunk)
      {
        world.GetChunkStorage().SaveChunk(coord, *chunk, FolderPath,
                                          *world.BlockRegistry);
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
    CurrentPhase = Phase::Done;
    Active = false;
    Report(sink, "done", 1.f, "World saved.");
    break;
  }
  case Phase::GenerateColumns:
  {
    if (AdvanceGeneration(world, budget * CHUNK_SIZE))
    {
      if (world.WorldGen)
      {
        world.SpawnPoint = world.WorldGen->DefaultSpawnPosition(0, 0);
      }
      CurrentPhase = Phase::PostCreate;
    }
    const float frac =
        0.05f + 0.75f * (static_cast<float>(GenDoneColumns) /
                         static_cast<float>(std::max(1, GenTotalColumns)));
    Report(sink, "generate", frac, "Generating terrain...");
    break;
  }
  case Phase::PostCreate:
  {
    world.WorldName = TargetWorldName;
    world.WorldGenSetsData = BuildDefaultWorldGenSets();
    world.RebuildResolvedObjectFeatures();
    world.AllowProceduralFill = world.IsStreamingEnabled();
    world.InitStreamerCallbacks();
    BeginMeshWarmup(world);
    Report(sink, "mesh_warmup", 0.82f, "Building terrain meshes...");
    break;
  }
  case Phase::Done:
    Active = false;
    return true;
  }

  return CurrentPhase == Phase::Done;
}

} // namespace cutum
