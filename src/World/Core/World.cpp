// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "World/Core/World.h"
#include "Activity/WorldCreatureActivitySink.h"
#include "App/Settings/RenderSettings.h"
#include "Core/Progress/IProgressSink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Primitives/Cube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Storage/Object.h"
#include "Storage/ObjectStorage.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/WorldCooperativeOps.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/LegacyChunkJsonLoader.h"
#include "World/Math/GridMath.h"
#include "World/Prefabs/Prefab.h"
#include "World/Prefabs/PrefabUtil.h"
#include "World/Raycast/BlockRaycast.h"
#include "WorldGen/Core/IChunkPopulator.h"
#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr float kMaxReasonablePlayerY = 512.0f;

constexpr float kMinReasonablePlayerY = -32.0f;

bool HasChunkDataFiles(const std::string &chunks_dir)
{
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return false;
  }
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const auto ext = entry.path().extension();
    if (ext == ".json" || ext == ".cchunk")
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool UWorld::HasPersistedTerrainOnDisk(const std::string &world_folder_path)
{
  const std::string blocks_file = world_folder_path + "/blocks.json";
  if (std::filesystem::exists(blocks_file) &&
      std::filesystem::file_size(blocks_file) > 2)
  {
    return true;
  }

  const std::string chunks_dir = world_folder_path + "/chunks";
  if (HasChunkDataFiles(chunks_dir) ||
      UChunkStorageService::HasChunkFilesOnDisk(world_folder_path))
  {
    return true;
  }

  const std::string chunks_file = world_folder_path + "/chunks.json";
  if (!std::filesystem::exists(chunks_file))
  {
    return false;
  }

  try
  {
    std::ifstream file(chunks_file);
    if (!file.is_open())
    {
      return false;
    }
    const json data = json::parse(file);
    const std::string storage = data.value("storage", "");
    if (storage == "per_file" || storage == "binary" || storage == "json")
    {
      return true;
    }
    return data.contains("chunks") && data["chunks"].is_array() &&
           !data["chunks"].empty();
  }
  catch (const json::exception &)
  {
    return false;
  }
}

UWorld::UWorld(std::shared_ptr<UObjectStorage> object_storage,
               std::shared_ptr<UViewEngine> views)
    : ObjectStorageInstance(object_storage), ViewInstance(views)
{
  if (ObjectStorageInstance && ObjectStorageInstance->GetTextureCubeStorage())
  {
    BlockRegistry = std::make_unique<UBlockRegistry>(
        ObjectStorageInstance->GetTextureCubeStorage(), BlockDefinitions);
  }
  IsIntersectionExists = false;
  HasIntersectionBlock = false;
  RegisterDefaultCreaturePosePresenters(PosePresenterRegistry);
  ChunkStorage = std::make_unique<UChunkStorageService>();
}

void UWorld::GenerateUsers()
{
  AddUser("Username");
  ApplySpawnToCamera();
}

std::string UWorld::GetWorldName() const { return WorldName; }

void UWorld::SetWorldName(const std::string &value) { WorldName = value; }

glm::vec3 UWorld::GetSpawnPoint() const { return SpawnPoint; }

void UWorld::SetSpawnPoint(glm::vec3 value) { SpawnPoint = value; }

void UWorld::SetTerrainParams(uint32_t Seed, const std::string &terrainType)
{
  WorldSeed = Seed;
  TerrainType = terrainType;
  ProceduralSettings settings;
  settings.Seed = Seed;
  settings.Generator = ProceduralGeneratorFromString(terrainType);
  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);
  SetProceduralSettings(settings);
  if (BlockRegistry && !Streamer)
  {
    Streamer = std::make_unique<UChunkStreamer>(BlockWorld, *BlockRegistry,
                                                WorldSeed, 0, 8);
  }
}

void UWorld::SetProceduralSettings(const ProceduralSettings &settings,
                                   bool rebuildPipeline)
{
  ProceduralTemplate = settings;
  WorldSeed = settings.Seed;
  TerrainType = ProceduralGeneratorToString(settings.Generator);
  MaxLoadOpsPerFrame = settings.MaxLoadOpsPerFrame;
  MaxUnloadOpsPerFrame = settings.MaxUnloadOpsPerFrame;
  if (BlockRegistry && !Streamer)
  {
    Streamer = std::make_unique<UChunkStreamer>(BlockWorld, *BlockRegistry,
                                                WorldSeed, 0, 8);
  }
  if (rebuildPipeline)
  {
    RebuildWorldGenPipeline();
  }
}

void UWorld::RebuildWorldGenPipeline()
{
  if (!BlockRegistry)
  {
    WorldGen.reset();
    return;
  }
  WorldGenContext ctx{BlockWorld, *BlockRegistry, ProceduralTemplate,
                      PrefabLibrary};
  ctx.WorldgenOwnerPackId = WorldgenOwnerPackId;
  ctx.OnColumnMeshDirty = [this](int world_x, int world_z, int min_y, int max_y)
  { MarkColumnMeshDirty(world_x, world_z, min_y, max_y); };
  WorldGen = UProceduralWorldGenFactory::Create(ctx);
}

void UWorld::SetRenderDistanceChunks(int distance)
{
  RenderDistanceChunks = distance;
  EffectiveRenderDistance = distance;
  if (Streamer)
  {
    Streamer->SetRenderDistance(distance);
  }
  MeshCache.SetRenderDistanceChunks(distance);
}

void UWorld::SetChunkWriteFormat(ChunkWriteFormat format)
{
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
  ChunkStorage->SetWriteFormat(format);
}

ChunkWriteFormat UWorld::GetChunkWriteFormat() const
{
  return ChunkStorage ? ChunkStorage->GetSettings().writeFormat
                      : ChunkWriteFormat::Binary;
}

void UWorld::InitChunkScheduler()
{
  if (!BlockRegistry)
  {
    ChunkPopulator.reset();
    ChunkScheduler.reset();
    return;
  }
  ChunkPopulator = std::make_unique<UPipelineChunkPopulator>(
      *BlockRegistry, PrefabLibrary, WorldgenOwnerPackId);
  ChunkScheduler =
      std::make_unique<UChunkLoadScheduler>(*ChunkPopulator, ChunkGenTokens);
  ChunkScheduler->SetMarkDirtyFn(
      [this](glm::ivec3 coord)
      {
        if (Streamer)
        {
          Streamer->NotifyChunkCommitted(coord);
        }
        else
        {
          MeshCache.MarkDirty(coord);
        }
      });
  ChunkScheduler->SetColumnMeshDirtyFn(
      [this](glm::ivec3 groundCoord, int min_y, int max_y)
      { MarkTerrainChunkMeshDirty(groundCoord, min_y, max_y); });
  if (!AsyncChunkIo)
  {
    AsyncChunkIo = std::make_unique<UAsyncChunkIO>();
  }
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
}

void UWorld::TickAsyncChunkSystems()
{
  if (ChunkScheduler && ProceduralTemplate.AsyncChunkGeneration)
  {
    const int commits =
        LastMovementSpeed > ProceduralTemplate.MovementSpeedBoostThreshold
            ? ProceduralTemplate.MaxChunkCommitsPerFrameBoost
            : ProceduralTemplate.MaxChunkCommitsPerFrame;
    const int load_ops =
        LastMovementSpeed > ProceduralTemplate.MovementSpeedBoostThreshold
            ? ProceduralTemplate.MaxLoadOpsPerFrameBoost
            : MaxLoadOpsPerFrame;
    ChunkScheduler->Tick(BlockWorld, commits, load_ops);
  }
  if (!ChunkStorage)
  {
    return;
  }

  if (AsyncChunkIo && ProceduralTemplate.AsyncChunkIo)
  {
    for (AsyncChunkLoadResult &load : AsyncChunkIo->DrainLoads())
    {
      const glm::ivec3 ground(load.coord.x, 0, load.coord.z);
      bool columnCompleted = false;
      const auto pendingIt = PendingAsyncColumnLoadSlices.find(ground);
      if (pendingIt != PendingAsyncColumnLoadSlices.end())
      {
        const int remaining = --pendingIt->second;
        if (remaining <= 0)
        {
          PendingAsyncColumnLoadSlices.erase(pendingIt);
          columnCompleted = true;
        }
      }
      else
      {
        columnCompleted = true;
      }

      if (load.success &&
          load.token.IsValidFor(load.coord, load.token.sequence))
      {
        const UChunkBuffer buffer = ChunkStorage->DeserializeChunk(
            load.payload, load.coord, load.format, *BlockRegistry);
        if (!buffer.IsEmpty())
        {
          buffer.ApplyTo(BlockWorld);
        }
      }

      if (columnCompleted && Streamer)
      {
        Streamer->NotifyChunkCommitted(ground);
      }
    }

    for (AsyncChunkSaveRequest &save : AsyncChunkIo->DrainSaves())
    {
      if (ChunkStorage->GetSettings().writeFormat == ChunkWriteFormat::Binary &&
          ChunkStorage->GetSettings().deleteLegacyJsonOnBinarySave)
      {
        const std::string legacyJson = ChunkStorage->ChunkFilePath(
            WorldFolderPath, save.coord, ChunkDiskFormat::Json);
        std::error_code ec;
        std::filesystem::remove(legacyJson, ec);
      }
      auto pendingIt = PendingAsyncColumnSaveSlices.find(save.groundCoord);
      if (pendingIt != PendingAsyncColumnSaveSlices.end())
      {
        --pendingIt->second;
        if (pendingIt->second <= 0)
        {
          PendingAsyncColumnSaveSlices.erase(pendingIt);
          ChunkStorage->ClearColumnSavePending(save.groundCoord);
        }
      }
      else
      {
        ChunkStorage->ClearColumnSavePending(save.groundCoord);
      }
    }
  }
}

void UWorld::InitStreamerCallbacks()
{
  if (!Streamer || !BlockRegistry)
  {
    return;
  }
  InitChunkScheduler();
  Streamer->SetRenderDistance(RenderDistanceChunks);
  Streamer->SetMaxLoadOpsPerFrame(MaxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(MaxUnloadOpsPerFrame);
  Streamer->SetMaxTerrainHeight(ProceduralTemplate.MaxHeight);
  Streamer->SetEnabled(StreamingEnabled);
  Streamer->SetWorldFolder(WorldFolderPath);
  Streamer->SetCallbacks(
      [this](glm::ivec3 coord)
      {
        if (!ChunkStorage)
        {
          return false;
        }
        if (ChunkStorage->IsColumnSavePending(coord))
        {
          return false;
        }
        if (IsTerrainColumnDiskLoadPending(coord))
        {
          return false;
        }
        if (ProceduralTemplate.AsyncChunkIo && AsyncChunkIo)
        {
          RequestAsyncTerrainColumnLoad(coord);
          return false;
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        const bool loaded =
            ChunkStorage->LoadTerrainColumn(coord, BlockWorld, WorldFolderPath,
                                            *BlockRegistry,
                                            ProceduralTemplate.MaxHeight) > 0;
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
        return loaded;
      },
      [this](glm::ivec3 coord)
      {
        if (!ChunkStorage)
        {
          return;
        }
        const glm::ivec3 ground(coord.x, 0, coord.z);
        const auto t0 = std::chrono::high_resolution_clock::now();
        if (ProceduralTemplate.AsyncChunkIo && AsyncChunkIo)
        {
          RequestAsyncTerrainColumnSave(ground);
        }
        else
        {
          ChunkStorage->SaveTerrainColumn(ground, BlockWorld, WorldFolderPath,
                                          *BlockRegistry,
                                          ProceduralTemplate.MaxHeight);
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
      [this](glm::ivec3 coord)
      {
        MarkTerrainChunkMeshDirty(glm::ivec3(coord.x, 0, coord.z), 0,
                                  ProceduralTemplate.MaxHeight);
      },
      [this](int x, int z)
      {
        if (!AllowProceduralFill || !WorldGen)
        {
          return;
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        WorldGen->GenerateColumn(x, z);
        FrameStreamingGenMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
      },
      [this](glm::ivec3 coord)
      {
        MeshCache.RemoveChunk(coord);
        if (coord.y == 0 && ChunkScheduler)
        {
          ChunkScheduler->Invalidate(coord);
        }
      });
  Streamer->SetAsyncGeneration(ProceduralTemplate.AsyncChunkGeneration);
  Streamer->SetAsyncCallbacks(
      [this](glm::ivec3 coord, int priority)
      {
        if (ChunkScheduler)
        {
          glm::ivec2 column_origin(0);
          bool has_origin = false;
          if (auto camera = GetCurrentUserCamera())
          {
            const PlayerCapsule cap = camera->GetPlayerCapsule();
            const glm::vec3 feet(camera->GetPosition().x,
                                 cap.feetY(camera->GetPosition()) + 0.01f,
                                 camera->GetPosition().z);
            const glm::ivec3 feet_block = WorldPosToBlock(feet);
            column_origin = glm::ivec2(feet_block.x, feet_block.z);
            has_origin = true;
          }
          ChunkScheduler->RequestLoad(coord, priority, ProceduralTemplate,
                                      column_origin, has_origin);
        }
      },
      [this](glm::ivec3 coord)
      {
        if (ChunkScheduler && ChunkScheduler->IsPending(coord))
        {
          return false;
        }
        if (!BlockWorld.GetChunkManager().HasChunk(coord))
        {
          return false;
        }
        return IsTerrainChunkComplete(BlockWorld, coord,
                                      ProceduralTemplate.MaxHeight);
      });
  Streamer->SetColumnPendingCallback(
      [this](glm::ivec3 coord)
      { return IsTerrainColumnDiskLoadPending(coord); });
}

void UWorld::RefreshStreamerSettings()
{
  if (!Streamer)
  {
    return;
  }
  Streamer->SetAsyncGeneration(ProceduralTemplate.AsyncChunkGeneration);
  Streamer->SetMaxTerrainHeight(ProceduralTemplate.MaxHeight);
  Streamer->SetMaxLoadOpsPerFrame(MaxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(MaxUnloadOpsPerFrame);
}

void UWorld::RequestAsyncTerrainColumnLoad(glm::ivec3 groundCoord)
{
  if (!AsyncChunkIo || !ChunkStorage || !BlockRegistry)
  {
    return;
  }
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(groundCoord) ||
      PendingAsyncColumnLoadSlices.count(groundCoord) > 0)
  {
    return;
  }
  const int maxCy =
      (ProceduralTemplate.MaxHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const int sliceCount = maxCy + 1;
  PendingAsyncColumnLoadSlices[groundCoord] = sliceCount;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    AsyncChunkIo->RequestLoad(slice, *ChunkStorage, WorldFolderPath,
                              ChunkGenTokens.Current(groundCoord));
  }
}

void UWorld::RequestAsyncTerrainColumnSave(glm::ivec3 groundCoord)
{
  if (!AsyncChunkIo || !ChunkStorage || !BlockRegistry)
  {
    return;
  }
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(groundCoord) ||
      PendingAsyncColumnSaveSlices.count(groundCoord) > 0)
  {
    return;
  }
  const int maxCy =
      (ProceduralTemplate.MaxHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;
  int saveCount = 0;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    if (!BlockWorld.GetChunkManager().HasChunk(slice))
    {
      continue;
    }
    ++saveCount;
  }
  if (saveCount == 0)
  {
    return;
  }
  ChunkStorage->MarkColumnSavePending(groundCoord);
  PendingAsyncColumnSaveSlices[groundCoord] = saveCount;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    if (!BlockWorld.GetChunkManager().HasChunk(slice))
    {
      continue;
    }
    AsyncChunkIo->RequestSave(slice, *ChunkStorage, WorldFolderPath, BlockWorld,
                              *BlockRegistry,
                              ChunkGenTokens.Current(groundCoord));
  }
}

bool UWorld::IsTerrainColumnDiskLoadPending(glm::ivec3 groundCoord) const
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  return PendingAsyncColumnLoadSlices.count(groundCoord) > 0;
}

void UWorld::LoadInitialStreamingChunks()
{
  if (!ChunkStorage || !BlockRegistry)
  {
    return;
  }
  const glm::ivec3 spawnBlock = WorldPosToBlock(SpawnPoint);
  const glm::ivec3 centerChunk = UChunkManager::WorldToChunk(spawnBlock);
  const int radius = RenderDistanceChunks + 1;
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      ChunkStorage->LoadTerrainColumn(
          glm::ivec3(centerChunk.x + dx, 0, centerChunk.z + dz), BlockWorld,
          WorldFolderPath, *BlockRegistry, ProceduralTemplate.MaxHeight);
    }
  }
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorld.GetChunkManager().ForEachChunk(
      [this](const UChunk &chunk) { MeshCache.MarkDirty(chunk.GetCoord()); });
}

void UWorld::GenerateWorldBlocks()
{
  if (!BlockRegistry)
  {
    return;
  }
  if (HasPersistedSave || LoadedFromChunkSave)
  {
    std::cerr << "GenerateWorldBlocks: skipped (persisted world on disk)"
              << std::endl;
    return;
  }
  if (!WorldGen)
  {
    RebuildWorldGenPipeline();
  }
  if (!WorldGen)
  {
    return;
  }

  const int patchRadiusBlocks = std::max(1, RenderDistanceChunks) * CHUNK_SIZE;
  if (StreamingEnabled)
  {
    WorldGen->GenerateSpawnPatch(0, 0, patchRadiusBlocks);
  }
  else
  {
    WorldGen->GenerateFullPatch(0, 0, patchRadiusBlocks);
  }
  SpawnPoint = WorldGen->DefaultSpawnPosition(0, 0);

  RebuildBlockMesh();
}

void UWorld::SetBlockDefinitionStorage(
    std::shared_ptr<UBlockDefinitionStorage> definitions)
{
  BlockDefinitions = std::move(definitions);
  if (BlockRegistry)
  {
    BlockRegistry->SetDefinitions(BlockDefinitions);
  }
  else if (ObjectStorageInstance &&
           ObjectStorageInstance->GetTextureCubeStorage())
  {
    BlockRegistry = std::make_unique<UBlockRegistry>(
        ObjectStorageInstance->GetTextureCubeStorage(), BlockDefinitions);
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
    }
  }
}

void UWorld::SetBlockMergeRegistry(
    std::shared_ptr<UBlockMergeRegistry> merge_registry)
{
  BlockMergeRegistry = std::move(merge_registry);
  if (BlockRegistry)
  {
    BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
  }
}

void UWorld::OnBlockRegistryChanged()
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  RebuildBlockMesh();
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnBlockRegistryRuntimeOverlayChanged()
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  MeshCache.MarkAllDirtyFromWorld(BlockWorld);
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnCreatureCatalogChanged()
{
  ReloadAllCreatureVisuals();
  if (OnCreatureCatalogChangedCallback)
  {
    OnCreatureCatalogChangedCallback();
  }
}

void UWorld::ReloadAllCreatureVisuals()
{
  ForEachCreature(
      [this](UCreature &creature)
      {
        const CreatureDefinition *def =
            GetCreatureDefinition(creature.GetTypeId());
        if (!def)
        {
          return;
        }
        creature.SetVisual(CreateCreatureVisual(*def));
      });
}

void UWorld::WaitForPendingMeshJobs() { MeshCache.WaitForAsyncMeshIdle(); }

void UWorld::RefreshBlockRegistry()
{
  WaitForPendingMeshJobs();
  if (BlockRegistry)
  {
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
      if (BlockDefinitions)
      {
        BlockMergeRegistry->PopulateBlockDefinitionStorage(*BlockDefinitions);
      }
    }
    if (BlockDefinitions)
    {
      BlockRegistry->SetDefinitions(BlockDefinitions);
    }
    BlockRegistry->Reload();
  }
}

void UWorld::RebuildBlockMesh()
{
  if (!BlockRegistry)
  {
    return;
  }
  MeshCache.RebuildAll(BlockWorld, *BlockRegistry);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorldReady = CachedBlockCount > 0;
}

bool UWorld::IsReasonablePlayerPosition(const glm::vec3 &position) const
{
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z))
  {
    return false;
  }
  if (position.y <= kMinReasonablePlayerY || position.y > kMaxReasonablePlayerY)
  {
    return false;
  }
  if (std::abs(position.x) > 100000.0f || std::abs(position.z) > 100000.0f)
  {
    return false;
  }
  return true;
}

void UWorld::SanitizeUserPosition(const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  if (!IsReasonablePlayerPosition(user->GetPosition()))
  {
    user->SetPosition(SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (std::abs(user->GetCameraPitch()) > 89.0f)
  {
    user->SetCameraOrientation(user->GetCameraYaw(), 0.0f);
  }
}

void UWorld::ApplySpawnToCamera()
{
  glm::vec3 spawn = SpawnPoint;
  const PlayerCapsule cap = PlayerCapsule::Standing();
  DepenetrateEye(spawn, cap, GetMovementCollisionSkipId());
  SpawnPoint = spawn;

  if (auto user = GetCurrentUser())
  {
    user->SetPosition(SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (auto camera = GetCurrentUserCamera())
  {
    camera->SetPosition(SpawnPoint);
    camera->SetOrientation(-90.0f, 0.0f);
    return;
  }
  if (ViewInstance && ViewInstance->GetActiveCamera())
  {
    ViewInstance->GetActiveCamera()->SetPosition(SpawnPoint);
    ViewInstance->GetActiveCamera()->SetOrientation(-90.0f, 0.0f);
  }
}

std::optional<int> UWorld::FindHighestSolidY(int x, int z) const
{
  if (!BlockRegistry)
  {
    return std::nullopt;
  }
  for (int y = 255; y >= 0; --y)
  {
    if (BlockRegistry->IsSolid(BlockWorld.GetBlock(glm::ivec3(x, y, z))))
    {
      return y;
    }
  }
  return std::nullopt;
}

std::optional<float> UWorld::QueryGroundFeetYColumn(int worldX,
                                                    int worldZ) const
{
  if (const std::optional<int> topY = FindHighestSolidY(worldX, worldZ))
  {
    return BlockTopY(*topY);
  }
  return std::nullopt;
}

std::optional<float> UWorld::QueryGroundFeetYUnder(int worldX, int worldZ,
                                                   float referenceFeetY) const
{
  if (!BlockRegistry)
  {
    return std::nullopt;
  }
  const int startY =
      std::clamp(static_cast<int>(std::floor(referenceFeetY + 0.25f)), 0, 255);
  for (int y = startY; y >= 0; --y)
  {
    if (BlockRegistry->IsSolid(
            BlockWorld.GetBlock(glm::ivec3(worldX, y, worldZ))))
    {
      return BlockTopY(y);
    }
  }
  return std::nullopt;
}

bool UWorld::IsValidStandCell(const glm::ivec3 &cell,
                              const PlayerCapsule &cap) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (!BlockRegistry->BlocksMovement(BlockWorld.GetBlock(cell)))
  {
    return false;
  }
  const std::optional<int> columnTopY = FindHighestSolidY(cell.x, cell.z);
  if (!columnTopY || *columnTopY != cell.y)
  {
    return false;
  }
  const int layers = static_cast<int>(std::ceil(cap.height));
  for (int dy = 1; dy <= layers; ++dy)
  {
    const glm::ivec3 above(cell.x, cell.y + dy, cell.z);
    if (BlockRegistry->BlocksMovement(BlockWorld.GetBlock(above)))
    {
      return false;
    }
  }
  return true;
}

namespace
{

constexpr int kFootprintMinSolidSamples = 4;

struct FootprintStandSampleStats
{
  int solidSamples{0};
  int validStandSamples{0};
  bool centerSolid{false};
  bool centerValidStand{false};
};

FootprintStandSampleStats
SampleFootprintAtFeet(const UWorld &world, const UBlockWorld &blockWorld,
                      const UBlockRegistry &registry, float feetY,
                      float centerX, float centerZ, float halfWidth,
                      const PlayerCapsule &cap, bool checkValidStand)
{
  FootprintStandSampleStats stats{};
  const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
  const int centerGx = WorldCoordToBlockIndex(centerX);
  const int centerGz = WorldCoordToBlockIndex(centerZ);
  const float sampleX[3] = {centerX - halfWidth, centerX, centerX + halfWidth};
  const float sampleZ[3] = {centerZ - halfWidth, centerZ, centerZ + halfWidth};
  for (float sx : sampleX)
  {
    for (float sz : sampleZ)
    {
      const glm::ivec3 cell(WorldCoordToBlockIndex(sx), supportY,
                            WorldCoordToBlockIndex(sz));
      const bool solid = registry.BlocksMovement(blockWorld.GetBlock(cell));
      if (!solid)
      {
        continue;
      }
      ++stats.solidSamples;
      const bool isCenter = cell.x == centerGx && cell.z == centerGz;
      if (isCenter)
      {
        stats.centerSolid = true;
      }
      if (!checkValidStand)
      {
        continue;
      }
      if (!world.IsValidStandCell(cell, cap))
      {
        continue;
      }
      ++stats.validStandSamples;
      if (isCenter)
      {
        stats.centerValidStand = true;
      }
    }
  }
  return stats;
}

} // namespace

bool UWorld::IsValidStandFootprint(const glm::vec3 &eyePos,
                                   const PlayerCapsule &cap, float feetY) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const CollisionVolume vol = CollisionVolumeFromEye(eyePos, cap);
  const FootprintStandSampleStats stats = SampleFootprintAtFeet(
      *this, BlockWorld, *BlockRegistry, feetY, vol.center.x, vol.center.z,
      cap.halfWidth, cap, true);
  return stats.centerValidStand &&
         stats.validStandSamples >= kFootprintMinSolidSamples;
}

void UWorld::EnsurePlayerOnGround()
{
  auto user = GetCurrentUser();
  if (!user || !BlockRegistry)
  {
    return;
  }

  std::string userName = CurrentUserName;
  for (const auto &entry : Users)
  {
    if (entry.second == user)
    {
      userName = entry.first;
      break;
    }
  }
  auto camera = GetUserCamera(userName);
  if (!camera)
  {
    return;
  }

  const PlayerCapsule cap = PlayerCapsule::Standing();
  glm::vec3 pos = user->GetPosition();
  const glm::ivec3 column = WorldPosToBlock(pos);
  int x = column.x;
  int z = column.z;

  std::optional<int> topY = FindHighestSolidY(x, z);
  if (!topY)
  {
    const glm::ivec3 spawnColumn = WorldPosToBlock(SpawnPoint);
    x = spawnColumn.x;
    z = spawnColumn.z;
    topY = FindHighestSolidY(x, z);
  }
  if (!topY)
  {
    ApplySpawnToCamera();
    if (auto cam = GetUserCamera(userName))
    {
      cam->ResetVerticalPhysics();
    }
    return;
  }

  pos = BlockCenter(glm::ivec3(x, *topY, z));
  pos.y = BlockTopY(*topY) + cap.eyeHeight;
  DepenetrateEye(pos, cap, GetMovementCollisionSkipId());

  user->SetPosition(pos);
  camera->SetPosition(pos);
  camera->ResetVerticalPhysics();
}

void UWorld::WarmupVisibleListAtCamera()
{
  auto camera = GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  const glm::mat4 vp = proj * view;
  MeshCache.UpdateVisibleInstances(Frustum::FromViewProjection(vp), vp,
                                   camera->GetPosition());
}

void UWorld::WarmupSpawnAreaForEnterGame()
{
  if (!BlockRegistry)
  {
    return;
  }

  InitStreamerCallbacks();
  if (auto user = GetCurrentUser())
  {
    ApplyUserToCamera(user);
  }
  else
  {
    ApplySpawnToCamera();
  }

  const int prev_load_ops = MaxLoadOpsPerFrame;
  const int warmup_load_ops =
      std::max(prev_load_ops,
               (2 * RenderDistanceChunks + 1) * (2 * RenderDistanceChunks + 1));
  MaxLoadOpsPerFrame = warmup_load_ops;
  if (Streamer)
  {
    Streamer->SetMaxLoadOpsPerFrame(warmup_load_ops);
  }

  constexpr int kMeshFlushBudget = 256;
  for (int pass = 0; pass < 48; ++pass)
  {
    UpdateStreaming();
    TickAsyncChunkSystems();
    MeshCache.RebuildDirtyChunks(BlockWorld, *BlockRegistry, kMeshFlushBudget,
                                 kMeshFlushBudget);
    MeshCache.DrainAsyncMeshResults(BlockWorld, *BlockRegistry,
                                    kMeshFlushBudget);
    if (!MeshCache.HasPendingDirty() && !MeshCache.HasPendingAsyncMeshWork())
    {
      break;
    }
  }

  MeshCache.WaitForAsyncMeshIdle();
  while (MeshCache.HasPendingDirty())
  {
    MeshCache.RebuildDirtyChunks(BlockWorld, *BlockRegistry, kMeshFlushBudget,
                                 kMeshFlushBudget);
    MeshCache.DrainAsyncMeshResults(BlockWorld, *BlockRegistry,
                                    kMeshFlushBudget);
  }

  MaxLoadOpsPerFrame = prev_load_ops;
  RefreshStreamerSettings();
  WarmupVisibleListAtCamera();
}

void UWorld::FinalizePlayerAfterWorldLoad()
{
  ResetMeshLoadDiagnostics();
  BlockCounter.MarkNeedsRecount();
  bool has_terrain_chunks = false;
  BlockWorld.GetChunkManager().ForEachChunk([&](const UChunk &)
                                            { has_terrain_chunks = true; });
  BlockWorldReady = has_terrain_chunks || CachedBlockCount > 0;
  PhysicsSuspendFrames = 3;

  if (CurrentUserName.empty() && !Users.empty())
  {
    SetCurrentUserName(Users.begin()->first);
  }
  if (ControlledCreatureId == 0)
  {
    if (PlayerCreatureId != 0)
    {
      SetControlledCreature(PlayerCreatureId);
    }
    else if (auto user = GetCurrentUser())
    {
      if (user->GetPlayerCreatureId() != 0)
      {
        PlayerCreatureId = user->GetPlayerCreatureId();
        SetControlledCreature(PlayerCreatureId);
      }
    }
  }

  if (auto user = GetCurrentUser())
  {
    SanitizeUserPosition(user);
    if (BlockWorldReady)
    {
      EnsurePlayerOnGround();
    }
    else
    {
      ApplySpawnToCamera();
    }
  }
  else
  {
    ApplySpawnToCamera();
  }

  if (auto camera = GetCurrentUserCamera())
  {
    camera->ResetVerticalPhysics();
  }
}

void UWorld::ApplyUserToCamera(const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  SanitizeUserPosition(user);

  std::string userName = CurrentUserName;
  for (const auto &entry : Users)
  {
    if (entry.second == user)
    {
      userName = entry.first;
      break;
    }
  }
  if (auto camera = GetUserCamera(userName))
  {
    camera->SetPosition(user->GetPosition());
    camera->SetOrientation(user->GetCameraYaw(), user->GetCameraPitch());
  }
}

void UWorld::Create(const std::string &world_name)
{
  UNullProgressSink sink;
  BeginCooperativeCreate(world_name);
  while (!TickCooperativeCreate(sink, 64))
  {
  }
}

void UWorld::Load(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeLoad(world_folder_path);
  while (!TickCooperativeLoad(sink, 64))
  {
  }
}

void UWorld::Save(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeSave(world_folder_path);
  while (!TickCooperativeSave(sink, 64))
  {
  }
}

void UWorld::BeginCooperativeLoad(const std::string &world_folder_path)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginLoad(*this, world_folder_path);
}

bool UWorld::TickCooperativeLoad(IProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeSave(const std::string &world_folder_path)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginSave(*this, world_folder_path);
}

bool UWorld::TickCooperativeSave(IProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeCreate(const std::string &world_name)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginCreate(*this, world_name);
}

bool UWorld::TickCooperativeCreate(IProgressSink &sink, int columnBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, columnBudget);
}

bool UWorld::HasActiveCooperativeOperation() const
{
  return CoopSession && CoopSession->Active;
}

bool UWorld::AddObject(const std::string type_id, const glm::vec3 &position)
{
  if (!BlockRegistry)
  {
    return false;
  }
  const BlockId Id = BlockRegistry->GetIdByTypeName(type_id);
  if (Id == BLOCK_AIR)
  {
    std::cerr << "World::AddObject: Unknown block type '" << type_id << "'"
              << std::endl;
    return false;
  }
  const glm::ivec3 blockPos = WorldPosToBlock(position);
  if (!BlockWorld.IsAir(blockPos))
  {
    return false;
  }
  BlockWorld.SetBlock(blockPos, Id);
  if (BlockWorld.GetBlock(blockPos) != Id)
  {
    return false;
  }
  ++CachedBlockCount;
  BlockWorldReady = true;
  MarkBlockChunkDirty(blockPos);
  return true;
}

bool UWorld::PlacePrefab(const std::string &prefab_name,
                         glm::ivec3 anchorWorldPos)
{
  if (!PrefabLibrary || !BlockRegistry)
  {
    return false;
  }
  const Prefab *prefab = PrefabLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }

  const PrefabPlacementStats stats =
      PlacePrefabAt(BlockWorld, *prefab, anchorWorldPos, true);
  if (stats.placedCount == 0)
  {
    return false;
  }
  for (const auto &voxel : prefab->voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab->anchor;
    if (BlockWorld.GetBlock(worldPos) == voxel.Id)
    {
      MarkBlockChunkDirty(worldPos);
    }
  }
  return true;
}

bool UWorld::CanPlacePrefab(const std::string &prefab_name,
                            glm::ivec3 anchorWorldPos) const
{
  if (!PrefabLibrary || !BlockRegistry)
  {
    return false;
  }
  const Prefab *prefab = PrefabLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }
  return CanPlacePrefabAt(BlockWorld, *prefab, anchorWorldPos);
}

std::optional<glm::ivec3>
UWorld::FindPrefabAnchorFromView(const glm::vec3 &position,
                                 const glm::vec3 &front) const
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return std::nullopt;
  }
  glm::ivec3 normal = hit->faceNormal;
  if (normal == glm::ivec3(0))
  {
    const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
    if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
        std::abs(toCamera.x) >= std::abs(toCamera.z))
    {
      normal.x = toCamera.x > 0.0f ? 1 : -1;
    }
    else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
    {
      normal.y = toCamera.y > 0.0f ? 1 : -1;
    }
    else
    {
      normal.z = toCamera.z > 0.0f ? 1 : -1;
    }
  }
  return hit->blockPos + normal;
}

bool UWorld::AddUser(const std::string &Name)
{
  if (Users.find(Name) != Users.end())
    return false;

  if (Name.empty())
    return false;

  Users[Name] = std::make_shared<UUser>();
  auto user = Users[Name];
  const glm::vec3 eyeOffset(0.0f, 1.62f, 0.0f);
  const glm::vec3 bodyOrigin = BodyOriginFromEye(SpawnPoint, eyeOffset);
  std::string speciesId = "human";
  if (CreatureDefinitions)
  {
    const std::string controlled =
        CreatureDefinitions->GetControlledDefaultSpeciesId();
    if (!controlled.empty())
    {
      speciesId = controlled;
    }
  }
  const CreatureId pid = SpawnCreature(speciesId, bodyOrigin);
  user->SetPlayerCreatureId(pid);
  if (UPlayer *player = dynamic_cast<UPlayer *>(GetCreature(pid)))
  {
    player->BindUser(user);
    if (!user->GetSelectedSkinId().empty())
    {
      player->SetSkinId(user->GetSelectedSkinId());
      if (const CreatureDefinition *def = GetCreatureDefinition(speciesId))
      {
        player->SetVisual(CreateCreatureVisual(*def));
      }
    }
    UCreatureInventory &inv = player->GetInventory();
    inv.InitCreativeDefaults();
    inv.EnsureDefaultHotbar();
  }
  if (Users.size() == 1)
  {
    PlayerCreatureId = pid;
    ControlledCreatureId = pid;
  }
  auto camera = std::make_shared<UCamera>(
      SpawnPoint, glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
  camera->SetFreeMove(false);
  const size_t viewId = ViewInstance->AddCameraReturnId(camera);
  user->SetViewId(viewId);
  if (Users.size() == 1)
  {
    SetCurrentUserName(Name);
    ViewInstance->SetActiveCamera(viewId);
  }

  return true;
}

void UWorld::DelUser(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return;

  Users.erase(Name);
}

std::shared_ptr<UUser> UWorld::GetUser(const std::string &Name)
{
  auto I = Users.find(Name);
  return (I != Users.end()) ? I->second : nullptr;
}

const std::string &UWorld::GetCurrentUserName() const
{
  return CurrentUserName;
}

std::shared_ptr<UUser> UWorld::GetCurrentUser()
{
  return GetUser(CurrentUserName);
}

std::shared_ptr<UUser> UWorld::GetCurrentUser() const
{
  return const_cast<UWorld *>(this)->GetUser(CurrentUserName);
}

UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user)
{
  if (!user || user->GetPlayerCreatureId() == 0)
  {
    return nullptr;
  }
  if (UCreature *creature = GetCreature(user->GetPlayerCreatureId()))
  {
    return &creature->GetInventory();
  }
  return nullptr;
}

const UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user) const
{
  return const_cast<UWorld *>(this)->GetPlayerInventory(user);
}

void UWorld::EnsurePlayerHotbarCount(const std::shared_ptr<UUser> &user,
                                     size_t barCount)
{
  if (UCreatureInventory *inv = GetPlayerInventory(user))
  {
    inv->EnsureHotbarCount(barCount);
  }
}

bool UWorld::SetCurrentUserName(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return false;
  CurrentUserName = Name;
  if (auto user = GetCurrentUser())
  {
    if (user->GetPlayerCreatureId() != 0)
    {
      PlayerCreatureId = user->GetPlayerCreatureId();
      SetControlledCreature(PlayerCreatureId);
    }
  }
  ApplyUserToCamera(GetCurrentUser());
  if (auto user = GetCurrentUser())
  {
    ViewInstance->SetActiveCamera(user->GetViewId());
  }
  return true;
}

std::shared_ptr<UCamera> UWorld::GetUserCamera(const std::string &Name)
{
  auto user = GetUser(Name);
  if (user == nullptr)
    return nullptr;

  return ViewInstance->GetCamera(user->GetViewId());
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera()
{
  auto user = GetCurrentUser();
  if (user == nullptr)
    return nullptr;

  return ViewInstance->GetCamera(user->GetViewId());
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera() const
{
  auto user = GetCurrentUser();
  if (user == nullptr)
  {
    return nullptr;
  }
  return ViewInstance->GetCamera(user->GetViewId());
}

bool UWorld::AddObjectByView()
{
  return AddObjectByView(GetCurrentUserCamera()->GetPosition(),
                         GetCurrentUserCamera()->GetFront());
}

bool UWorld::DelObjectByView()
{
  return DelObjectByView(GetCurrentUserCamera()->GetPosition(),
                         GetCurrentUserCamera()->GetFront());
}

bool UWorld::CheckRayIntersection(
    const glm::vec3 &position, const glm::vec3 &front,
    std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
        &distance_map) const
{
  distance_map.clear();
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  const glm::vec3 hitCenter = BlockCenter(hit->blockPos);
  distance_map[hit->distance] =
      std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>(
          0, glm::vec3(hit->faceNormal), hitCenter, 0, 0);
  return true;
}

bool UWorld::CheckRayIntersection(const glm::vec3 &position,
                                  const glm::vec3 &front,
                                  glm::vec3 &intersecion, float &distance,
                                  size_t &cube_index, int &cube_side,
                                  size_t &object_index) const
{
  std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
      distance_map;

  const bool result = CheckRayIntersection(position, front, distance_map);
  if (result)
  {
    cube_side = std::get<0>(distance_map.begin()->second);
    intersecion = std::get<2>(distance_map.begin()->second);
    distance = distance_map.begin()->first;
    cube_index = 0;
    object_index = 0;
  }
  return result;
}

bool UWorld::CheckPositionFree(const glm::vec3 &position, float /*size*/) const
{
  return BlockWorld.IsAir(WorldPosToBlock(position));
}

std::optional<glm::vec3>
UWorld::FindNearestFreeCubePosition(const glm::vec3 &position,
                                    const glm::vec3 &front,
                                    const PlayerCapsule &cap) const
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return std::nullopt;
  }

  glm::ivec3 normal = hit->faceNormal;
  if (normal == glm::ivec3(0))
  {
    const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
    if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
        std::abs(toCamera.x) >= std::abs(toCamera.z))
    {
      normal.x = toCamera.x > 0.0f ? 1 : -1;
    }
    else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
    {
      normal.y = toCamera.y > 0.0f ? 1 : -1;
    }
    else
    {
      normal.z = toCamera.z > 0.0f ? 1 : -1;
    }
  }

  const glm::ivec3 placePos = hit->blockPos + normal;
  if (!BlockWorld.IsAir(placePos))
  {
    return std::nullopt;
  }

  const glm::vec3 res_position = BlockCenter(placePos);
  if (!CheckPositionFree(res_position, 1.0f))
  {
    return std::nullopt;
  }

  const CollisionVolume vol = CollisionVolumeFromEye(position, cap);
  const glm::vec3 blockCenter = BlockCenter(placePos);
  const glm::vec3 blockHalf(0.5f);
  if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, blockCenter,
                                blockHalf))
  {
    return std::nullopt;
  }

  return res_position;
}

bool UWorld::AddObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  auto user = GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const std::string &blockType =
      controlled->GetInventory().GetActiveBlockTypeName();
  if (blockType.empty())
  {
    return false;
  }

  PlayerCapsule cap = PlayerCapsule::Standing();
  if (const auto camera = GetCurrentUserCamera())
  {
    cap = camera->GetPlayerCapsule();
  }

  auto object_pos = FindNearestFreeCubePosition(position, front, cap);
  if (object_pos.has_value())
  {
    if (AddObject(blockType, object_pos.value()))
    {
      UpdateIntersection(position, front);
      return true;
    }
  }
  return false;
}

bool UWorld::PlaceActivePrefabByView()
{
  return PlaceActivePrefabByView(GetCurrentUserCamera()->GetPosition(),
                                 GetCurrentUserCamera()->GetFront());
}

bool UWorld::PlaceActivePrefabByView(const glm::vec3 &position,
                                     const glm::vec3 &front)
{
  auto user = GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const std::string &prefabName =
      controlled->GetInventory().GetActivePrefabName();
  if (prefabName.empty())
  {
    return false;
  }

  const auto anchor = FindPrefabAnchorFromView(position, front);
  if (!anchor.has_value())
  {
    return false;
  }
  if (PlacePrefab(prefabName, anchor.value()))
  {
    UpdateIntersection(position, front);
    return true;
  }
  return false;
}

bool UWorld::DelBlockAt(glm::ivec3 blockPos)
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (BlockWorld.GetBlock(blockPos) == BLOCK_AIR)
  {
    return false;
  }
  BlockWorld.SetBlock(blockPos, BLOCK_AIR);
  if (CachedBlockCount > 0)
  {
    --CachedBlockCount;
  }
  MarkBlockChunkDirty(blockPos);
  if (auto camera = GetCurrentUserCamera())
  {
    UpdateIntersection(camera->GetPosition(), camera->GetFront());
  }
  return true;
}

bool UWorld::DelObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  return DelBlockAt(hit->blockPos);
}

void UWorld::StartBreakSession(glm::ivec3 blockPos)
{
  BlockBreakSession session;
  session.blockPos = blockPos;
  session.progress = 0.f;
  BreakSession = session;
}

void UWorld::CancelBreakSession() { BreakSession.reset(); }

void UWorld::TickBreakSession(float dt, float durationSeconds)
{
  if (!BreakSession || durationSeconds <= 0.f)
  {
    return;
  }
  BreakSession->progress =
      std::min(1.f, BreakSession->progress + dt / durationSeconds);
}

bool UWorld::CompleteBreakSession()
{
  if (!BreakSession)
  {
    return false;
  }
  const glm::ivec3 pos = BreakSession->blockPos;
  BreakSession.reset();
  return DelBlockAt(pos);
}

float UWorld::GetBreakProgress() const
{
  return BreakSession ? BreakSession->progress : 0.f;
}

std::optional<glm::ivec3> UWorld::GetBreakSessionBlockPos() const
{
  if (!BreakSession)
  {
    return std::nullopt;
  }
  return BreakSession->blockPos;
}

bool UWorld::IsCameraInsideFluid(const glm::vec3 &eye, BlockId *outFluid) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const glm::ivec3 cell = WorldPosToBlock(eye);
  const BlockId Id = BlockWorld.GetBlock(cell);
  if (Id == BLOCK_AIR || BlockRegistry->BlocksMovement(Id))
  {
    return false;
  }
  const glm::vec3 center = BlockCenter(cell);
  const glm::vec3 rel = eye - center;
  if (std::abs(rel.x) > 0.5f || std::abs(rel.y) > 0.5f ||
      std::abs(rel.z) > 0.5f)
  {
    return false;
  }
  if (outFluid)
  {
    *outFluid = Id;
  }
  return true;
}

UWorld::SampledFluidState
UWorld::SampleFluidPhysicsVolume(const CollisionVolume &vol) const
{
  SampledFluidState state;
  if (!BlockRegistry)
  {
    return state;
  }
  std::unordered_map<BlockId, int> fluidWeights;
  const glm::vec3 center = vol.center;
  const glm::vec3 half = vol.halfExtents;
  const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
  const int radius =
      static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
  const glm::vec3 blockHalf(0.5f);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
        const BlockId Id = BlockWorld.GetBlock(blockPos);
        if (Id == BLOCK_AIR || BlockRegistry->BlocksMovement(Id))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (!UCube::CheckAabbCollision(center, half, blockCenter, blockHalf))
        {
          continue;
        }
        const auto &mov = BlockRegistry->Physics(Id).Movement;
        state.inFluid = true;
        fluidWeights[Id] += 1;
        state.DragHorizontal =
            std::max(state.DragHorizontal, mov.DragHorizontal);
        state.SinkSpeed = std::max(state.SinkSpeed, mov.SinkSpeed);
        state.RiseSpeed = std::max(state.RiseSpeed, mov.RiseSpeed);
      }
    }
  }
  if (state.inFluid)
  {
    state.blendWeight = 1.0f;
    int bestWeight = 0;
    for (const auto &entry : fluidWeights)
    {
      if (entry.second > bestWeight)
      {
        bestWeight = entry.second;
        state.dominantFluid = entry.first;
      }
    }
  }
  return state;
}

UWorld::SampledFluidState
UWorld::SampleFluidPhysics(const glm::vec3 &eyePos,
                           const PlayerCapsule &cap) const
{
  return SampleFluidPhysicsVolume(CollisionVolumeFromEye(eyePos, cap));
}

bool UWorld::CheckBlockCollisionVolume(const CollisionVolume &vol) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const glm::vec3 center = vol.center;
  const glm::vec3 half = vol.halfExtents;
  const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
  const int radius =
      static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
  const glm::vec3 blockHalf(0.5f);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
        const BlockId Id = BlockWorld.GetBlock(blockPos);
        if (!BlockRegistry->BlocksMovement(Id))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (UCube::CheckAabbCollision(center, half, blockCenter, blockHalf))
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool UWorld::CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                          CreatureId skipCreatureId) const
{
  for (const auto &entry : Creatures)
  {
    if (entry.first == skipCreatureId)
    {
      continue;
    }
    const CollisionVolume other = entry.second->GetCollisionVolume();
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, other.center,
                                  other.halfExtents))
    {
      return true;
    }
  }
  return false;
}

bool UWorld::CheckCollisionVolume(const CollisionVolume &vol,
                                  CreatureId skipCreatureId) const
{
  if (CheckBlockCollisionVolume(vol))
  {
    return true;
  }
  if (!EntityCollisionEnabled)
  {
    return false;
  }
  return CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const
{
  return CheckCollision(eyePos, cap, GetMovementCollisionSkipId());
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return CheckCollisionVolume(CollisionVolumeFromEye(eyePos, cap),
                              skipCreatureId);
}

bool UWorld::DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  constexpr int kMaxIterations = 32;
  constexpr float kStep = 0.05f;
  if (!CheckCollision(eyePos, cap, skipCreatureId))
  {
    return true;
  }
  for (int i = 0; i < kMaxIterations; ++i)
  {
    eyePos.y += kStep;
    if (!CheckCollision(eyePos, cap, skipCreatureId))
    {
      return true;
    }
  }
  return !CheckCollision(eyePos, cap, skipCreatureId);
}

bool UWorld::HasGroundSupportVolume(const CollisionVolume &vol,
                                    float feetY) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const FootprintStandSampleStats stats = SampleFootprintAtFeet(
      *this, BlockWorld, *BlockRegistry, feetY, vol.center.x, vol.center.z,
      vol.halfExtents.x, PlayerCapsule{}, false);
  return stats.centerSolid && stats.solidSamples >= kFootprintMinSolidSamples;
}

bool UWorld::HasGroundSupport(const glm::vec3 &eyePos,
                              const PlayerCapsule &cap) const
{
  return HasGroundSupportVolume(CollisionVolumeFromEye(eyePos, cap),
                                cap.feetY(eyePos));
}

namespace
{

constexpr float kCollisionMaxStep = 0.25f;
constexpr float kCollisionEpsilon = 0.01f;
constexpr int kCollisionMaxIterations = 64;

glm::vec3 ResolveMovementAxisBody(const UWorld &world,
                                  const glm::vec3 &fromBody, float axisDelta,
                                  int axis, const glm::vec3 &currentSizeBlocks,
                                  CreatureId skipCreatureId)
{
  if (std::abs(axisDelta) < 1e-8f)
  {
    return fromBody;
  }
  const float sign = axisDelta > 0.0f ? 1.0f : -1.0f;
  float remaining = std::abs(axisDelta);
  glm::vec3 body = fromBody;
  glm::vec3 axisUnit(0.0f);
  axisUnit[axis] = 1.0f;

  int iterations = 0;
  while (remaining > 1e-6f && iterations < kCollisionMaxIterations)
  {
    const float step = std::min(remaining, kCollisionMaxStep);
    const glm::vec3 nextBody = body + axisUnit * step * sign;
    if (world.CheckCollisionVolume(
            CollisionVolumeFromBody(nextBody, currentSizeBlocks),
            skipCreatureId))
    {
      glm::vec3 lo = body;
      glm::vec3 hi = nextBody;
      for (int i = 0; i < 8; ++i)
      {
        const glm::vec3 mid = (lo + hi) * 0.5f;
        if (world.CheckCollisionVolume(
                CollisionVolumeFromBody(mid, currentSizeBlocks),
                skipCreatureId))
        {
          hi = mid;
        }
        else
        {
          lo = mid;
        }
      }
      if (axis == 1 && sign < 0.0f)
      {
        body = lo;
      }
      else
      {
        body = lo - axisUnit * kCollisionEpsilon * sign;
      }
      break;
    }
    body = nextBody;
    remaining -= step;
    ++iterations;
  }
  return body;
}

} // namespace

glm::vec3 UWorld::ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                      const glm::vec3 &delta,
                                      const glm::vec3 &currentSizeBlocks,
                                      CreatureId skipCreatureId) const
{
  if (glm::dot(delta, delta) < 1e-10f)
  {
    return bodyOrigin;
  }
  glm::vec3 body = bodyOrigin;
  body = ResolveMovementAxisBody(*this, body, delta.y, 1, currentSizeBlocks,
                                 skipCreatureId);
  body = ResolveMovementAxisBody(*this, body, delta.x, 0, currentSizeBlocks,
                                 skipCreatureId);
  body = ResolveMovementAxisBody(*this, body, delta.z, 2, currentSizeBlocks,
                                 skipCreatureId);
  return body;
}

glm::vec3 UWorld::ResolveMovement(const glm::vec3 &eyePos,
                                  const glm::vec3 &delta,
                                  const PlayerCapsule &cap,
                                  CreatureId skipCreatureId) const
{
  if (glm::dot(delta, delta) < 1e-10f)
  {
    return eyePos;
  }
  glm::vec3 resolvedEye = eyePos;
  if (delta.y > 0.0f && CheckCollision(resolvedEye, cap, skipCreatureId))
  {
    DepenetrateEye(resolvedEye, cap, skipCreatureId);
  }
  const glm::vec3 eyeOffset(0.0f, cap.eyeHeight, 0.0f);
  const glm::vec3 sizeBlocks(cap.halfWidth * 2.0f, cap.height,
                             cap.halfWidth * 2.0f);
  const glm::vec3 body = BodyOriginFromEye(resolvedEye, eyeOffset);
  const glm::vec3 newBody =
      ResolveMovementBody(body, delta, sizeBlocks, skipCreatureId);
  return BoundsEyePosition(newBody, eyeOffset);
}

namespace
{

glm::vec3 StepStandPosition(const glm::ivec3 &stepCell,
                            const PlayerCapsule &cap)
{
  const float feetY = BlockTopY(stepCell.y);
  return glm::vec3(static_cast<float>(stepCell.x), feetY + cap.eyeHeight,
                   static_cast<float>(stepCell.z));
}

bool FindSteppableLedge(const UWorld &world, const UBlockWorld &blockWorld,
                        const UBlockRegistry &registry, const glm::vec3 &eyePos,
                        const glm::vec3 &dir, const PlayerCapsule &cap,
                        glm::ivec3 &outStepCell)
{
  const int dx = dir.x > 0.25f ? 1 : (dir.x < -0.25f ? -1 : 0);
  const int dz = dir.z > 0.25f ? 1 : (dir.z < -0.25f ? -1 : 0);
  if (dx == 0 && dz == 0)
  {
    return false;
  }
  const float feetY = cap.feetY(eyePos);
  const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
  const glm::ivec3 standCell(WorldCoordToBlockIndex(eyePos.x), supportY,
                             WorldCoordToBlockIndex(eyePos.z));
  if (!registry.BlocksMovement(blockWorld.GetBlock(standCell)))
  {
    return false;
  }
  const glm::ivec3 riserCell(standCell.x + dx, supportY, standCell.z + dz);
  if (!registry.BlocksMovement(blockWorld.GetBlock(riserCell)))
  {
    return false;
  }
  const glm::ivec3 stepCell(standCell.x + dx, supportY + 1, standCell.z + dz);
  if (!world.IsValidStandCell(stepCell, cap))
  {
    return false;
  }
  const float stepFeetY = BlockTopY(stepCell.y);
  const float rise = stepFeetY - feetY;
  if (rise < 0.45f || rise > 1.05f)
  {
    return false;
  }
  const glm::vec3 landingEye = StepStandPosition(stepCell, cap);
  if (world.CheckCollision(landingEye, cap))
  {
    return false;
  }
  outStepCell = stepCell;
  return true;
}

float DistanceToStepRiser(const glm::vec3 &eyePos, const glm::ivec3 &stepCell,
                          const glm::vec3 &dir, const PlayerCapsule &cap)
{
  const glm::vec3 blockCenter(static_cast<float>(stepCell.x),
                              static_cast<float>(stepCell.y),
                              static_cast<float>(stepCell.z));
  const glm::vec3 facePoint =
      blockCenter - glm::vec3(dir.x * 0.5f, 0.0f, dir.z * 0.5f);
  const glm::vec3 playerLead =
      eyePos + glm::vec3(dir.x * cap.halfWidth, 0.0f, dir.z * cap.halfWidth);
  return glm::dot(facePoint - playerLead, dir);
}

} // namespace

UWorld::StepUpProbe UWorld::ProbeStepUp(const glm::vec3 &eyePos,
                                        const glm::vec3 &horiz,
                                        const PlayerCapsule &cap,
                                        float maxTriggerDistance) const
{
  StepUpProbe probe{};
  if (!BlockRegistry)
  {
    return probe;
  }
  const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
  const float horizLen = glm::length(horizFlat);
  if (horizLen < 1e-6f)
  {
    return probe;
  }
  const glm::vec3 dir = horizFlat / horizLen;
  glm::ivec3 stepCell(0);
  if (!FindSteppableLedge(*this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
                          stepCell))
  {
    return probe;
  }
  const float dist = DistanceToStepRiser(eyePos, stepCell, dir, cap);
  if (dist < -0.02f || dist > maxTriggerDistance)
  {
    return probe;
  }
  probe.Valid = true;
  probe.DistanceToLedge = dist;
  probe.MoveDir = dir;
  probe.TargetPos = StepStandPosition(stepCell, cap);
  return probe;
}

bool UWorld::GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                              const PlayerCapsule &cap,
                              float maxTriggerDistance,
                              glm::vec3 &outLanding) const
{
  const StepUpProbe probe = ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
  if (!probe.Valid)
  {
    return false;
  }
  glm::ivec3 stepCell(0);
  const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
  const float horizLen = glm::length(horizFlat);
  if (horizLen < 1e-6f)
  {
    return false;
  }
  const glm::vec3 dir = horizFlat / horizLen;
  if (!FindSteppableLedge(*this, BlockWorld, *BlockRegistry, eyePos, dir, cap,
                          stepCell))
  {
    return false;
  }
  const glm::ivec3 feetCell =
      WorldPosToBlock(glm::vec3(eyePos.x, cap.feetY(eyePos) + 0.01f, eyePos.z));
  if (feetCell.x == stepCell.x && feetCell.z == stepCell.z &&
      feetCell.y >= stepCell.y)
  {
    return false;
  }

  outLanding = probe.TargetPos - glm::vec3(probe.MoveDir.x * 0.18f, 0.0f,
                                           probe.MoveDir.z * 0.18f);
  return !CheckCollision(outLanding, cap);
}

bool UWorld::TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                       const PlayerCapsule &cap, float maxTriggerDistance) const
{
  glm::vec3 landing = eyePos;
  if (!GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance, landing))
  {
    return false;
  }
  eyePos = landing;
  return true;
}

void UWorld::LoadUsers(const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open users file: " << file_name << std::endl;
    return;
  }

  try
  {
    Users.clear();
    json d = json::parse(val);
    for (auto I = d.begin(); I != d.end(); ++I)
    {
      const auto user_name = I.key();
      const auto user_data = I.value();

      AddUser(user_name);
      auto user = GetUser(user_name);
      if (!user)
      {
        continue;
      }

      glm::vec3 position = SpawnPoint;
      const auto position_value = user_data.value("position", json::array());
      if (position_value.is_array() && position_value.size() == 3)
      {
        position = glm::vec3(position_value[0].get<float>(),
                             position_value[1].get<float>(),
                             position_value[2].get<float>());
      }
      user->SetPosition(position);
      SanitizeUserPosition(user);

      if (user_data.contains("player_creature_id"))
      {
        const CreatureId savedId =
            user_data["player_creature_id"].get<CreatureId>();
        if (GetCreature(savedId))
        {
          user->SetPlayerCreatureId(savedId);
          PlayerCreatureId = savedId;
        }
      }
      if (user_data.contains("selected_skin_id"))
      {
        user->SetSelectedSkinId(
            user_data["selected_skin_id"].get<std::string>());
      }
      else if (user_data.contains("selected_appearance_type"))
      {
        user->SetSelectedAppearanceTypeId(
            user_data["selected_appearance_type"].get<std::string>());
        user->SetSelectedSkinId(
            user_data["selected_appearance_type"].get<std::string>());
      }
      UCreature *playerCreature = GetCreature(user->GetPlayerCreatureId());
      if (!playerCreature && PlayerCreatureId != 0)
      {
        user->SetPlayerCreatureId(PlayerCreatureId);
        playerCreature = GetCreature(PlayerCreatureId);
      }
      if (!playerCreature)
      {
        std::string speciesId = "human";
        if (CreatureDefinitions)
        {
          const std::string controlled =
              CreatureDefinitions->GetControlledDefaultSpeciesId();
          if (!controlled.empty())
          {
            speciesId = controlled;
          }
        }
        const glm::vec3 eyeOffset(0.0f, 1.62f, 0.0f);
        const glm::vec3 bodyOrigin = BodyOriginFromEye(position, eyeOffset);
        const CreatureId pid = SpawnCreature(speciesId, bodyOrigin);
        if (pid != 0)
        {
          user->SetPlayerCreatureId(pid);
          PlayerCreatureId = pid;
          if (Users.size() == 1)
          {
            ControlledCreatureId = pid;
          }
          if (UPlayer *player = dynamic_cast<UPlayer *>(GetCreature(pid)))
          {
            player->BindUser(user);
          }
          playerCreature = GetCreature(pid);
        }
      }
      if (playerCreature)
      {
        const glm::vec3 eyeOffset = playerCreature->GetEyeOffset();
        playerCreature->SetBodyOrigin(
            BodyOriginFromEye(user->GetPosition(), eyeOffset));
      }

      float yaw = -90.0f;
      float pitch = 0.0f;
      if (user_data.contains("yaw"))
      {
        yaw = user_data["yaw"].get<float>();
      }
      if (user_data.contains("pitch"))
      {
        pitch = user_data["pitch"].get<float>();
      }
      user->SetCameraOrientation(yaw, pitch);

      const size_t HotbarCount = 2;
      if (playerCreature)
      {
        UCreatureInventory &inv = playerCreature->GetInventory();
        const bool hadHotbars =
            user_data.contains("hotbars") && user_data["hotbars"].is_array();
        inv.DeserializeFromJson(user_data, HotbarCount);
        if (inv.GetStorage().empty())
        {
          inv.InitCreativeDefaults();
        }
        if (!hadHotbars || inv.IsPrimaryHotbarEmpty())
        {
          inv.EnsureDefaultHotbar();
        }
        playerCreature->SetOrientation(ModelYawFromCameraYaw(yaw), pitch);
        if (!user->GetSelectedSkinId().empty())
        {
          playerCreature->SetSkinId(user->GetSelectedSkinId());
          if (const CreatureDefinition *def =
                  GetCreatureDefinition(playerCreature->GetTypeId()))
          {
            playerCreature->SetVisual(CreateCreatureVisual(*def));
          }
        }
      }

      if (auto camera = GetUserCamera(user_name))
      {
        camera->SetPosition(position);
        camera->SetOrientation(yaw, pitch);
      }
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadUsers: " << e.what() << std::endl;
  }
}

void UWorld::SaveUsers(const std::string &file_name)
{
  json objects;

  for (auto I = Users.begin(); I != Users.end(); ++I)
  {
    const auto &user_name = I->first;
    auto user = I->second;

    glm::vec3 position = user->GetPosition();
    float yaw = user->GetCameraYaw();
    float pitch = user->GetCameraPitch();
    if (user_name == CurrentUserName)
    {
      if (auto camera = GetUserCamera(user_name))
      {
        position = camera->GetPosition();
        yaw = camera->GetYaw();
        pitch = camera->GetPitch();
        user->SetPosition(position);
        user->SetCameraOrientation(yaw, pitch);
      }
    }

    json user_json;
    user_json["position"] = json::array({position.x, position.y, position.z});
    user_json["yaw"] = yaw;
    user_json["pitch"] = pitch;
    user_json["player_creature_id"] = user->GetPlayerCreatureId();
    if (!user->GetSelectedSkinId().empty())
    {
      user_json["selected_skin_id"] = user->GetSelectedSkinId();
    }
    else if (!user->GetSelectedAppearanceTypeId().empty())
    {
      user_json["selected_appearance_type"] =
          user->GetSelectedAppearanceTypeId();
    }

    if (UCreature *playerCreature = GetCreature(user->GetPlayerCreatureId()))
    {
      playerCreature->GetInventory().SerializeToJson(user_json);
    }

    objects[user_name] = user_json;
  }

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << objects.dump(4);
    file.close();
  }
}

void UWorld::LoadWorldData(const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open world data file: " << file_name << std::endl;
    return;
  }

  try
  {
    json d = json::parse(val);
    std::string world_name_value = d.value("world_name", "");
    json spawn_point_value = d.value("spawn_point", json::array());

    if (world_name_value.empty() || spawn_point_value.empty())
      return;

    if (!spawn_point_value.is_array())
      return;

    if (spawn_point_value.size() != 3)
      return;

    glm::vec3 spawn_point(spawn_point_value[0].get<float>(),
                          spawn_point_value[1].get<float>(),
                          spawn_point_value[2].get<float>());

    WorldName = world_name_value;
    SpawnPoint = spawn_point;

    if (d.contains("terrain") && d["terrain"].is_string())
    {
      TerrainType = d["terrain"].get<std::string>();
    }
    if (d.contains("world_seed"))
    {
      WorldSeed = d["world_seed"].get<uint32_t>();
    }
    if (d.contains("procedural") && d["procedural"].is_object())
    {
      ProceduralTemplate = ParseProceduralSettings(d);
      TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
      WorldSeed = ProceduralTemplate.Seed;
    }
    else
    {
      ProceduralTemplate.Seed = WorldSeed;
      ProceduralTemplate.Generator = ProceduralGeneratorFromString(TerrainType);
      ResolveProceduralDefaults(ProceduralTemplate);
      ApplyGeneratorTierDefaults(ProceduralTemplate);
    }
    ResourcePacksEnabled.clear();
    ResourcePacksPrimary.clear();
    ResourcePacksSecondary.clear();
    WorldgenOwnerPackId.clear();
    if (d.contains("resource_packs") && d["resource_packs"].is_object())
    {
      const auto &rp = d["resource_packs"];
      auto parseIds =
          [](const nlohmann::json &arr, std::vector<std::string> &out)
      {
        if (!arr.is_array())
        {
          return;
        }
        out.reserve(arr.size());
        for (const auto &id : arr)
        {
          if (id.is_string())
          {
            out.push_back(id.get<std::string>());
          }
        }
      };
      if (rp.contains("primary") && rp["primary"].is_array())
      {
        parseIds(rp["primary"], ResourcePacksPrimary);
      }
      if (rp.contains("secondary") && rp["secondary"].is_array())
      {
        parseIds(rp["secondary"], ResourcePacksSecondary);
      }
      if (ResourcePacksPrimary.empty() && rp.contains("enabled") &&
          rp["enabled"].is_array())
      {
        parseIds(rp["enabled"], ResourcePacksPrimary);
      }
      if (rp.contains("worldgen_owner") && rp["worldgen_owner"].is_string())
      {
        WorldgenOwnerPackId = rp["worldgen_owner"].get<std::string>();
      }
      ResourcePacksEnabled = ResourcePacksPrimary;
      ResourcePacksEnabled.insert(ResourcePacksEnabled.end(),
                                  ResourcePacksSecondary.begin(),
                                  ResourcePacksSecondary.end());
    }
    if (d.contains("catalog_fingerprint") &&
        d["catalog_fingerprint"].is_string())
    {
      CatalogFingerprint = d["catalog_fingerprint"].get<std::string>();
    }
    else
    {
      CatalogFingerprint.clear();
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadWorldData: " << e.what()
              << std::endl;
  }
}

void UWorld::SaveWorldData(const std::string &file_name)
{
  json world_data;

  world_data["world_name"] = WorldName;
  world_data["terrain"] = TerrainType;
  world_data["world_seed"] = WorldSeed;
  WriteProceduralSettings(world_data, ProceduralTemplate);

  json arr = json::array({SpawnPoint.x, SpawnPoint.y, SpawnPoint.z});
  world_data["spawn_point"] = arr;

  if (!ResourcePacksPrimary.empty() || !ResourcePacksSecondary.empty())
  {
    auto &rp = world_data["resource_packs"];
    if (!ResourcePacksPrimary.empty())
    {
      rp["primary"] = ResourcePacksPrimary;
    }
    if (!ResourcePacksSecondary.empty())
    {
      rp["secondary"] = ResourcePacksSecondary;
    }
    if (!WorldgenOwnerPackId.empty())
    {
      rp["worldgen_owner"] = WorldgenOwnerPackId;
    }
  }
  else if (!ResourcePacksEnabled.empty())
  {
    world_data["resource_packs"]["primary"] = ResourcePacksEnabled;
  }

  if (!CatalogFingerprint.empty())
  {
    world_data["catalog_fingerprint"] = CatalogFingerprint;
  }

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << world_data.dump(4);
    file.close();
  }
}

void UWorld::SaveMovementDiagnostics(const std::string &file_name) const
{
  json root;
  root["schema"] = "movement_diagnostics.v2";
  root["world_name"] = WorldName;
  root["sample_count"] = MovementDiagHistory.size();
  json samples = json::array();
  for (const MovementDiagnostics &sample : MovementDiagHistory)
  {
    samples.push_back({
        {"delta_time", sample.deltaTime},
        {"player_y_drop", sample.playerYDrop},
        {"streaming_loads", sample.streamingLoads},
        {"streaming_unloads", sample.streamingUnloads},
        {"streaming_gen_ms", sample.streamingGenMs},
        {"streaming_io_ms", sample.streamingIoMs},
        {"mesh_rebuild_ms", sample.meshRebuildMs},
        {"dirty_chunks_pending", sample.dirtyChunksPending},
        {"mesh_rebuilds_this_frame", sample.meshRebuildsThisFrame},
        {"flat_rebuild_ms", sample.flatRebuildMs},
        {"count_non_air_ms", sample.countNonAirMs},
        {"async_mesh_in_flight", sample.asyncMeshInFlight},
        {"greedy_cache_entries", sample.greedyCacheEntries},
        {"frames_since_load", sample.framesSinceLoad},
        {"mesh_backlog_cleared", sample.meshBacklogCleared},
        {"hitch_detected", sample.hitchDetected},
        {"fall_through_suspected", sample.fallThroughSuspected},
    });
  }
  root["samples"] = std::move(samples);
  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << root.dump(2);
  }
}

void UWorld::AppendMovementDiagnosticsSample()
{
  constexpr size_t kMaxSamples = 4096;
  MovementDiagHistory.push_back(MovementDiag);
  if (MovementDiagHistory.size() > kMaxSamples)
  {
    const size_t trim = MovementDiagHistory.size() - kMaxSamples;
    MovementDiagHistory.erase(MovementDiagHistory.begin(),
                              MovementDiagHistory.begin() +
                                  static_cast<std::ptrdiff_t>(trim));
  }
}

void UWorld::DoMovement()
{
  if (!BlockWorldReady)
  {
    return;
  }
  if (PhysicsSuspendFrames > 0)
  {
    --PhysicsSuspendFrames;
    return;
  }

  auto t_begin = std::chrono::high_resolution_clock::now();
  FrameStreamingGenMs = 0.0;
  FrameStreamingIoMs = 0.0;

  auto camera = GetCurrentUserCamera();
  UCreature *controlled = GetControlledCreature();
  if (camera && camera->GetPosition().y < kMinReasonablePlayerY)
  {
    EnsurePlayerOnGround();
    camera->ResetVerticalPhysics();
    return;
  }
  const float prevPlayerY = camera ? camera->GetPosition().y : 0.0f;
  const float dt = camera ? camera->GetDeltaTime() : 0.0f;

  if (camera && Streamer && StreamingEnabled)
  {
    const glm::vec3 eyePos = camera->GetPosition();
    float feetY =
        FeetYFromEye(eyePos, controlled ? controlled->GetEyeOffset().y : 1.62f);
    if (controlled)
    {
      feetY = BoundsFeetY(controlled->GetBodyOrigin());
    }
    const glm::ivec3 feetBlock =
        WorldPosToBlock(glm::vec3(eyePos.x, feetY + 0.01f, eyePos.z));
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      Streamer->SetViewForward(forward);
    }
    Streamer->EnsureCollisionChunks(feetBlock);
  }

  UWorldCreatureActivitySink activitySink(*this);
  ActivityDirector.TickAgents(*this, activitySink, dt);

  ForEachCreature(
      [&](UCreature &creature)
      {
        if (ControlledCreatureId != 0 &&
            creature.GetId() == ControlledCreatureId)
        {
          return;
        }
        if (creature.IsPossessed())
        {
          return;
        }
        creature.ExecuteIntent(*this, dt);
      });

  bool is_moved = camera && camera->DoMovement(this);

  if (controlled && camera)
  {
    const glm::vec3 eye = camera->GetPosition();
    float feetY = FeetYFromEye(eye, controlled->GetEyeOffset().y);
    if (!camera->GetFreeMove() && camera->HasAnchoredFeet())
    {
      const int gx = WorldCoordToBlockIndex(eye.x);
      const int gz = WorldCoordToBlockIndex(eye.z);
      if (const std::optional<float> gy = QueryGroundFeetYUnder(gx, gz, feetY))
      {
        feetY = *gy;
      }
    }
    controlled->SetBodyOrigin(glm::vec3(eye.x, feetY, eye.z));
    controlled->GetLocomotion().SetStanceBlendForView(camera->GetStanceBlend());
    controlled->GetLocomotion().SyncFeetAnchorFromView(
        feetY, camera->HasAnchoredFeet());
    controlled->SetOrientation(ModelYawFromCameraYaw(camera->GetYaw()),
                               camera->GetPitch());
    controlled->SyncBoundsFromStance();
    controlled->GetLocomotion().SetMode(camera->GetFreeMove()
                                            ? CreatureMovementMode::Flying
                                            : CreatureMovementMode::Walking);
    float horizontalSpeed = 0.0f;
    const UCreatureLocomotionController &camLoc =
        camera->GetLocomotionController();
    const LocomotionState camState = camLoc.GetLocomotionState();
    const PlayerInput moveInput = camera->GetMovementInput();
    if (camState == LocomotionState::Walk || camState == LocomotionState::Run ||
        camState == LocomotionState::Crouch)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    else if (camState == LocomotionState::Fly ||
             camState == LocomotionState::Glide ||
             camState == LocomotionState::Hover)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    controlled->RebuildLocomotionFactsFromController(
        camLoc, controlled->GetLocomotion().GetCapabilities(),
        static_cast<float>(camera->GetDeltaTime()), horizontalSpeed, this);
    is_moved = true;
  }

  if (camera)
  {
    UpdateStreaming();
    TickAsyncChunkSystems();
    BlockWorldReady = true;
  }

  if (is_moved && camera)
  {
    if (auto user = GetCurrentUser())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
    UpdateIntersection(camera->GetPosition(), camera->GetFront());
  }

  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDoMovementMks = static_cast<uint64_t>(
      std::chrono::duration<double, std::micro>(t_end - t_begin).count());
  UpdateMovementDiagnostics(camera, prevPlayerY);
}

void UWorld::UpdateMovementDiagnostics(const std::shared_ptr<UCamera> &camera,
                                       float prevPlayerY)
{
  MovementDiag = MovementDiagnostics{};
  if (!camera)
  {
    return;
  }

  const glm::vec3 playerPos = camera->GetPosition();
  const PlayerCapsule cap = camera->GetPlayerCapsule();
  MovementDiag.feetBlock = WorldPosToBlock(
      glm::vec3(playerPos.x, cap.feetY(playerPos) + 0.01f, playerPos.z));
  MovementDiag.feetChunk = UChunkManager::WorldToChunk(MovementDiag.feetBlock);
  const glm::ivec3 feetGround(MovementDiag.feetChunk.x, 0,
                              MovementDiag.feetChunk.z);
  MovementDiag.feetChunkLoaded =
      BlockWorld.GetChunkManager().HasChunk(feetGround);
  MovementDiag.feetIsAir = BlockWorld.IsAir(MovementDiag.feetBlock);
  MovementDiag.meshDrawCount = GetRenderInstanceCount();
  MovementDiag.deltaTime = camera->GetDeltaTime();
  MovementDiag.streamingGenMs = FrameStreamingGenMs;
  MovementDiag.streamingIoMs = FrameStreamingIoMs;
  MovementDiag.dirtyChunksPending = static_cast<int>(MeshCache.GetDirtyCount());
  MovementDiag.flatRebuildMs = MeshCache.GetLastFlatRebuildMs();
  MovementDiag.asyncMeshInFlight = MeshCache.GetAsyncInFlightCount();
  MovementDiag.greedyCacheEntries =
      static_cast<int>(MeshCache.GetGreedyCacheSize());
  MovementDiag.framesSinceLoad = FramesSinceLoad;
  MovementDiag.meshBacklogCleared = MeshBacklogClearedLatch;
  TickMeshLoadDiagnostics();

  if (HasLastPlayerY)
  {
    MovementDiag.playerYDrop = prevPlayerY - playerPos.y;
  }
  else
  {
    MovementDiag.playerYDrop = 0.0f;
  }
  HasLastPlayerY = true;
  LastPlayerY = playerPos.y;

  if (Streamer)
  {
    const auto &stats = Streamer->GetLastFrameStats();
    MovementDiag.streamingLoads = stats.loadsThisFrame;
    MovementDiag.streamingUnloads = stats.unloadsThisFrame;
    for (const glm::ivec3 &coord : stats.unloadedCoords)
    {
      if (coord.x == feetGround.x && coord.z == feetGround.z)
      {
        MovementDiag.feetInUnloadList = true;
        break;
      }
    }
  }

  const double frameMs =
      (DurationDoMovementMks +
       (ViewInstance ? ViewInstance->GetDurationUpdateMks() : 0.0)) /
      1000.0;
  MovementDiag.hitchDetected = frameMs > 50.0 || MovementDiag.deltaTime > 0.1f;
  MovementDiag.fallThroughSuspected =
      MovementDiag.playerYDrop > 2.0f &&
      (MovementDiag.feetIsAir || !MovementDiag.feetChunkLoaded) &&
      MovementDiag.meshDrawCount > 0;

#ifdef CUBATARIUM_DEBUG
  if (MovementDiag.hitchDetected || MovementDiag.fallThroughSuspected ||
      MovementDiag.playerYDrop > 2.0f)
  {
    std::cerr << "[Movement-debug] cameraDt=" << MovementDiag.deltaTime
              << " frameMs=" << frameMs << " yDrop=" << MovementDiag.playerYDrop
              << " feetChunk=(" << MovementDiag.feetChunk.x << ","
              << MovementDiag.feetChunk.y << "," << MovementDiag.feetChunk.z
              << ")"
              << " hasChunk=" << MovementDiag.feetChunkLoaded
              << " feetAir=" << MovementDiag.feetIsAir
              << " meshDraw=" << MovementDiag.meshDrawCount
              << " loads=" << MovementDiag.streamingLoads
              << " unloads=" << MovementDiag.streamingUnloads
              << " feetUnloaded=" << MovementDiag.feetInUnloadList << std::endl;
  }
#endif
  AppendMovementDiagnosticsSample();
}

void UWorld::UpdateFrameHitchDiagnostics(double draw_scene_mks,
                                         double view_update_mks)
{
  DurationDrawSceneMks = static_cast<uint64_t>(draw_scene_mks);
  const double frameMs =
      (DurationDoMovementMks + view_update_mks + draw_scene_mks) / 1000.0;
  MovementDiag.hitchDetected = frameMs > 50.0 || MovementDiag.deltaTime > 0.1f;
}

void UWorld::ResetMeshLoadDiagnostics()
{
  FramesSinceLoad = 0;
  MeshBacklogClearedLatch = false;
  MeshLoadDiagActive = true;
}

void UWorld::TickMeshLoadDiagnostics()
{
  if (!MeshLoadDiagActive)
  {
    return;
  }
  if (FramesSinceLoad < 600 || MeshCache.HasPendingDirty() ||
      MeshCache.HasPendingAsyncMeshWork())
  {
    ++FramesSinceLoad;
  }
  if (!MeshBacklogClearedLatch && !MeshCache.HasPendingDirty() &&
      !MeshCache.HasPendingAsyncMeshWork())
  {
    MeshBacklogClearedLatch = true;
  }
  if (MeshBacklogClearedLatch && FramesSinceLoad >= 600 &&
      !MeshCache.HasPendingDirty())
  {
    MeshLoadDiagActive = false;
  }
}

void UWorld::UpdateStreaming()
{
  if (!Streamer || !StreamingEnabled)
  {
    return;
  }
  if (auto camera = GetCurrentUserCamera())
  {
    const PlayerCapsule cap = camera->GetPlayerCapsule();
    const glm::vec3 eye = camera->GetPosition();
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      Streamer->SetViewForward(forward);
    }
    if (Render.AltitudeAdaptiveFog)
    {
      AltitudeParams.AltitudeThresholdBlocks =
          Render.AltitudeFogThresholdBlocks;
      AltitudeParams.RenderDistancePenaltyPerChunk = 1;
      AltitudeParams.FogStartRatioBoost =
          std::max(0.15f, Render.AltitudeFogPenaltyPer16Blocks * 4.0f);
      const StreamingAltitudeSnapshot alt = ComputeStreamingAltitude(
          RenderDistanceChunks, eye.y, cap.feetY(eye),
          Render.DistanceFogStartRatio, AltitudeParams);
      EffectiveRenderDistance = alt.EffectiveRenderDistance;
      EffectiveFogStartRatio = alt.EffectiveFogStartRatio;
    }
    else
    {
      EffectiveRenderDistance = RenderDistanceChunks;
      EffectiveFogStartRatio = Render.DistanceFogStartRatio;
    }
    Streamer->SetRenderDistance(EffectiveRenderDistance);
    MeshCache.SetRenderDistanceChunks(EffectiveRenderDistance);

    const float dt = std::max(0.0001f, camera->GetDeltaTime());
    const glm::vec3 delta = eye - LastCameraPosition;
    LastMovementSpeed = glm::length(glm::vec3(delta.x, 0.0f, delta.z)) / dt;
    LastCameraPosition = eye;

    Streamer->Update(WorldPosToBlock(eye), eye, cap);
    Streamer->PrefetchAhead(
        UChunkManager::WorldToChunk(
            WorldPosToBlock(glm::vec3(eye.x, cap.feetY(eye) + 0.01f, eye.z))),
        forward, LastMovementSpeed,
        ProceduralTemplate.MovementSpeedBoostThreshold);
  }
}

size_t UWorld::GetRenderInstanceCount() const
{
  if (Render.GreedyMeshing)
  {
    return MeshCache.GetGreedyVertexCount();
  }
  return MeshCache.GetInstanceCount();
}

void UWorld::UpdateIntersection(const glm::vec3 &position,
                                const glm::vec3 &front)
{
  IsIntersectionExists = CheckRayIntersection(
      position, front, Intersection, IntersectionDistance,
      IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  HasIntersectionBlock = hit.has_value();
  PlaceTargetActive = false;
  if (hit)
  {
    IntersectionBlockPos = hit->blockPos;
    const auto camera = GetCurrentUserCamera();
    const PlayerCapsule cap =
        camera ? camera->GetPlayerCapsule() : PlayerCapsule::Standing();
    if (FindNearestFreeCubePosition(position, front, cap).has_value())
    {
      glm::ivec3 normal = hit->faceNormal;
      if (normal == glm::ivec3(0))
      {
        const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
        if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
            std::abs(toCamera.x) >= std::abs(toCamera.z))
        {
          normal.x = toCamera.x > 0.0f ? 1 : -1;
        }
        else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
        {
          normal.y = toCamera.y > 0.0f ? 1 : -1;
        }
        else
        {
          normal.z = toCamera.z > 0.0f ? 1 : -1;
        }
      }
      const glm::ivec3 placePos = hit->blockPos + normal;
      if (BlockWorld.IsAir(placePos))
      {
        PlaceTargetActive = true;
        PlaceBlockPos = placePos;
      }
    }
  }
  else
  {
    IntersectionBlockPos = glm::ivec3(0);
    PlaceBlockPos = glm::ivec3(0);
  }

  if (auto user = GetCurrentUser())
  {
    if (auto camera = GetCurrentUserCamera())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
  }
}

bool UWorld::GetIsIntersectionExists() const { return IsIntersectionExists; }

size_t UWorld::GetIntersectionObjectIndex() const
{
  return IntersectionObjectIndex;
}

size_t UWorld::GetIntersectionCubeIndex() const
{
  return IntersectionCubeIndex;
}

uint64_t UWorld::GetDurationDoMovementMks() const
{
  return DurationDoMovementMks;
}

void UWorld::InvalidateBlockMesh()
{
  if (BlockRegistry)
  {
    MeshCache.MarkAllDirtyFromWorld(BlockWorld);
  }
}

void UWorld::SetRenderSettings(const RenderSettings &settings)
{
  Render = settings;
  MeshCache.SetRenderSettings(settings);
}

const std::vector<FaceInstance> &UWorld::GetBlockRenderInstances()
{
  if (BlockRegistry && MeshCache.HasPendingDirty())
  {
    const auto t0 = std::chrono::high_resolution_clock::now();
    const size_t dirtyBefore = MeshCache.GetDirtyCount();
    MeshCache.RebuildDirtyChunks(BlockWorld, *BlockRegistry, 8, 8);
    MovementDiag.meshRebuildMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0)
            .count();
    MovementDiag.meshRebuildsThisFrame =
        static_cast<int>(dirtyBefore - MeshCache.GetDirtyCount());
    MovementDiag.dirtyChunksPending =
        static_cast<int>(MeshCache.GetDirtyCount());
    if (!MeshCache.HasPendingDirty())
    {
      const auto t0 = std::chrono::high_resolution_clock::now();
      BlockCounter.TickRecount(BlockWorld, 4);
      if (!BlockCounter.NeedsRecount())
      {
        CachedBlockCount = BlockCounter.GetCount();
      }
      MovementDiag.countNonAirMs =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - t0)
              .count();
    }
  }
  if (auto camera = GetCurrentUserCamera())
  {
    const auto flat_t0 = std::chrono::high_resolution_clock::now();
    const glm::mat4 view = camera->GetViewMatrix();
    const glm::mat4 proj = camera->GetProjection();
    const glm::mat4 vp = proj * view;
    MeshCache.UpdateVisibleInstances(Frustum::FromViewProjection(vp), vp,
                                     camera->GetPosition());
    MovementDiag.flatRebuildMs = MeshCache.GetLastFlatRebuildMs();
  }
  return MeshCache.GetFaceInstances();
}

const std::vector<GreedyMeshBatch> &UWorld::GetGreedyRenderBatches()
{
  GetBlockRenderInstances();
  return MeshCache.GetGreedyBatches();
}

size_t UWorld::GetGreedyVertexCount() const
{
  return MeshCache.GetGreedyVertexCount();
}

uint64_t UWorld::GetMeshRevision() const { return MeshCache.GetMeshRevision(); }

uint64_t UWorld::GetCullRevision() const { return MeshCache.GetCullRevision(); }

void UWorld::MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y)
{
  const glm::ivec3 base =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, min_y, world_z));
  const glm::ivec3 top =
      UChunkManager::WorldToChunk(glm::ivec3(world_x, max_y, world_z));
  std::unordered_set<glm::ivec3, IVec3Hash> dirty_chunks;
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int cy = base.y; cy <= top.y; ++cy)
      {
        dirty_chunks.insert(glm::ivec3(base.x + dx, cy, base.z + dz));
      }
    }
  }
  for (const glm::ivec3 &coord : dirty_chunks)
  {
    MeshCache.MarkDirty(coord);
  }
}

void UWorld::MarkTerrainChunkMeshDirty(glm::ivec3 groundChunkCoord, int min_y,
                                       int max_y)
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  for (int cx = groundChunkCoord.x - 1; cx <= groundChunkCoord.x + 1; ++cx)
  {
    for (int cz = groundChunkCoord.z - 1; cz <= groundChunkCoord.z + 1; ++cz)
    {
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        MeshCache.MarkDirty(glm::ivec3(cx, cy, cz));
      }
    }
  }
}

void UWorld::MarkBlockChunkDirty(glm::ivec3 blockPos)
{
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  ModifiedChunks.insert(chunkCoord);

  const bool immediate = BlockRegistry != nullptr;
  auto mark_coord = [&](glm::ivec3 coord)
  {
    if (immediate)
    {
      MeshCache.RebuildChunkImmediate(BlockWorld, *BlockRegistry, coord);
    }
    else
    {
      MeshCache.MarkDirty(coord);
    }
  };

  mark_coord(chunkCoord);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    mark_coord(UChunkManager::WorldToChunk(blockPos + offset));
  }
}

void UWorld::LoadBlocks(const std::string &file_name)
{
  if (!BlockRegistry)
  {
    return;
  }
  ULegacyChunkJsonLoader::LoadBlocksFile(BlockWorld, *BlockRegistry, file_name);
}

void UWorld::LoadChunks(const std::string &file_name)
{
  if (!BlockRegistry)
  {
    return;
  }
  ULegacyChunkJsonLoader::LoadMonolithicChunksFile(BlockWorld, *BlockRegistry,
                                                   file_name);
}

void UWorld::MigrateMonolithicChunksJson(const std::string & /*chunks_file*/,
                                         const std::string &world_folder)
{
  if (!ChunkStorage || !BlockRegistry)
  {
    return;
  }
  BlockWorld.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        ChunkStorage->SaveChunk(chunk.GetCoord(), chunk, world_folder,
                                *BlockRegistry);
      });
  ChunkStorage->WriteStorageMarker(world_folder);
}

void UWorld::MigrateObjectsFromJson(const std::string &file_name)
{
  std::ifstream file(file_name);
  if (!file.is_open())
  {
    return;
  }
  try
  {
    json objects = json::parse(file);
    for (const auto &object_data : objects)
    {
      const std::string type_name = object_data.value("type_name", "");
      if (type_name == "terrain_plane" || type_name.empty())
      {
        continue;
      }
      const auto position_value = object_data.value("position", json::array());
      if (!position_value.is_array() || position_value.size() != 3)
      {
        continue;
      }
      const glm::ivec3 blockPos(
          static_cast<int>(std::round(position_value[0].get<float>())),
          static_cast<int>(std::round(position_value[1].get<float>())),
          static_cast<int>(std::round(position_value[2].get<float>())));
      const BlockId Id = BlockRegistry->GetIdByTypeName(type_name);
      if (Id != BLOCK_AIR)
      {
        BlockWorld.SetBlock(blockPos, Id);
      }
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in MigrateObjectsFromJson: " << e.what()
              << std::endl;
  }
}

} // namespace cutum
