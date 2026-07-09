// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "World/Core/World.h"
#include "Activity/WorldCreatureActivitySink.h"
#include "App/Settings/RenderSettings.h"
#include "Core/Progress/IUProgressSink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/WorldFluidFacade.h"
#include "World/Core/WorldViewBinding.h"
#include "World/Diagnostics/MovementDiagnosticsRecorder.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshDirtyPolicy.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/PhysicsProfileFactory.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Physics/WorldChunkDirtyService.h"
#include "World/Physics/WorldMovementPhysicsService.h"
#include "World/Physics/WorldPhysicsScheduler.h"
#include "World/Raycast/BlockRaycast.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
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
constexpr float kSecondsPerMinute = 60.0f;
constexpr float kMinDayLengthMinutes = 1.0f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float Wrap01(float value)
{
  if (!std::isfinite(value))
  {
    return 0.0f;
  }
  const float wrapped = std::fmod(value, 1.0f);
  return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

std::string NormalizeToken(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                 { return static_cast<char>(std::tolower(ch)); });
  return value;
}

} // namespace

UWorld::UWorld(std::shared_ptr<UTextureCubeStorage> texture_cube,
               std::shared_ptr<UViewEngine> views)
    : TextureCubeInstance(texture_cube),
      ViewBinding(std::make_unique<UWorldViewBinding>(std::move(views))),
      MeshService(std::make_unique<UWorldMeshService>()),
      Streaming(std::make_unique<UWorldStreaming>()),
      Persistence(std::make_unique<UWorldPersistence>()), Environment(*this),
      Collision(BlockWorld, &Environment)
{
  if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    Collision.SetBlockRegistry(BlockRegistry.get());
  }
  IsIntersectionExists = false;
  HasIntersectionBlock = false;
  Environment.Initialize();
  ConfigurePhysicsServices();
}

UWorld::~UWorld() = default;

std::string UWorld::WeatherTypeToString(WeatherType value)
{
  switch (value)
  {
  case WeatherType::Cloudy:
    return "cloudy";
  case WeatherType::Rain:
    return "rain";
  case WeatherType::Storm:
    return "storm";
  case WeatherType::Snow:
    return "snow";
  case WeatherType::Clear:
  default:
    return "clear";
  }
}

bool UWorld::WeatherTypeFromString(const std::string &value, WeatherType &out)
{
  const std::string normalized = NormalizeToken(value);
  if (normalized == "clear")
  {
    out = WeatherType::Clear;
    return true;
  }
  if (normalized == "cloudy")
  {
    out = WeatherType::Cloudy;
    return true;
  }
  if (normalized == "rain")
  {
    out = WeatherType::Rain;
    return true;
  }
  if (normalized == "storm")
  {
    out = WeatherType::Storm;
    return true;
  }
  if (normalized == "snow")
  {
    out = WeatherType::Snow;
    return true;
  }
  return false;
}

void UWorld::SetTimeOfDayNormalized(float value)
{
  EnvironmentStateData.TimeOfDayNormalized = Wrap01(value);
}

void UWorld::AddTimeOfDayNormalized(float delta)
{
  SetTimeOfDayNormalized(EnvironmentStateData.TimeOfDayNormalized + delta);
}

void UWorld::SetDayLengthMinutes(float minutes)
{
  EnvironmentStateData.DayLengthMinutes =
      std::max(kMinDayLengthMinutes, minutes);
}

void UWorld::SetWeather(WeatherType weather, float transitionSeconds)
{
  EnvironmentStateData.TargetWeather = weather;
  EnvironmentStateData.WeatherTransitionDurationSec =
      std::max(0.0f, transitionSeconds);
  EnvironmentStateData.WeatherTransitionSec = 0.0f;
  if (EnvironmentStateData.Weather == weather ||
      EnvironmentStateData.WeatherTransitionDurationSec <= 0.01f)
  {
    EnvironmentStateData.Weather = weather;
    EnvironmentStateData.TargetWeather = weather;
    EnvironmentStateData.WeatherTransitionSec =
        EnvironmentStateData.WeatherTransitionDurationSec;
  }
}

void UWorld::SetWeatherByName(const std::string &name, float transitionSeconds)
{
  WeatherType weather = WeatherType::Clear;
  if (WeatherTypeFromString(name, weather))
  {
    SetWeather(weather, transitionSeconds);
  }
}

std::string UWorld::GetWeatherName() const
{
  return WeatherTypeToString(EnvironmentStateData.Weather);
}

void UWorld::TickEnvironment(float dtSeconds)
{
  if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds))
  {
    return;
  }

  if (!EnvironmentStateData.TimeFrozen)
  {
    const float cycle_seconds =
        std::max(kMinDayLengthMinutes, EnvironmentStateData.DayLengthMinutes) *
        kSecondsPerMinute;
    AddTimeOfDayNormalized(dtSeconds / std::max(1.0f, cycle_seconds));
  }

  const auto weather_to_cloudiness = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Cloudy:
      return 0.55f;
    case WeatherType::Rain:
      return 0.75f;
    case WeatherType::Storm:
      return 0.95f;
    case WeatherType::Snow:
      return 0.8f;
    case WeatherType::Clear:
    default:
      return 0.1f;
    }
  };
  const auto weather_to_precip = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Rain:
      return 0.6f;
    case WeatherType::Storm:
      return 1.0f;
    case WeatherType::Snow:
      return 0.55f;
    case WeatherType::Cloudy:
    case WeatherType::Clear:
    default:
      return 0.0f;
    }
  };
  const auto weather_to_fog = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Cloudy:
      return 1.05f;
    case WeatherType::Rain:
      return 1.2f;
    case WeatherType::Storm:
      return 1.35f;
    case WeatherType::Snow:
      return 1.25f;
    case WeatherType::Clear:
    default:
      return 1.0f;
    }
  };
  const auto weather_to_wind = [](WeatherType weather) -> float
  {
    switch (weather)
    {
    case WeatherType::Storm:
      return 1.0f;
    case WeatherType::Rain:
      return 0.65f;
    case WeatherType::Cloudy:
      return 0.45f;
    case WeatherType::Snow:
      return 0.35f;
    case WeatherType::Clear:
    default:
      return 0.2f;
    }
  };

  const bool transitioning =
      EnvironmentStateData.Weather != EnvironmentStateData.TargetWeather;
  if (transitioning &&
      EnvironmentStateData.WeatherTransitionDurationSec > 0.01f)
  {
    EnvironmentStateData.WeatherTransitionSec += dtSeconds;
    const float alpha =
        Clamp01(EnvironmentStateData.WeatherTransitionSec /
                EnvironmentStateData.WeatherTransitionDurationSec);
    if (alpha >= 1.0f)
    {
      EnvironmentStateData.Weather = EnvironmentStateData.TargetWeather;
      EnvironmentStateData.WeatherTransitionSec =
          EnvironmentStateData.WeatherTransitionDurationSec;
    }
    const float from_cloud =
        weather_to_cloudiness(EnvironmentStateData.Weather);
    const float to_cloud =
        weather_to_cloudiness(EnvironmentStateData.TargetWeather);
    const float from_precip = weather_to_precip(EnvironmentStateData.Weather);
    const float to_precip =
        weather_to_precip(EnvironmentStateData.TargetWeather);
    const float from_fog = weather_to_fog(EnvironmentStateData.Weather);
    const float to_fog = weather_to_fog(EnvironmentStateData.TargetWeather);
    const float from_wind = weather_to_wind(EnvironmentStateData.Weather);
    const float to_wind = weather_to_wind(EnvironmentStateData.TargetWeather);

    EnvironmentStateData.Cloudiness =
        from_cloud + (to_cloud - from_cloud) * alpha;
    EnvironmentStateData.PrecipitationIntensity =
        from_precip + (to_precip - from_precip) * alpha;
    EnvironmentStateData.WeatherFogMultiplier =
        from_fog + (to_fog - from_fog) * alpha;
    EnvironmentStateData.WindStrength =
        from_wind + (to_wind - from_wind) * alpha;
  }
  else
  {
    EnvironmentStateData.Weather = EnvironmentStateData.TargetWeather;
    EnvironmentStateData.Cloudiness =
        weather_to_cloudiness(EnvironmentStateData.Weather);
    EnvironmentStateData.PrecipitationIntensity =
        weather_to_precip(EnvironmentStateData.Weather);
    EnvironmentStateData.WeatherFogMultiplier =
        weather_to_fog(EnvironmentStateData.Weather);
    EnvironmentStateData.WindStrength =
        weather_to_wind(EnvironmentStateData.Weather);
  }

  const float solar_phase =
      std::sin(EnvironmentStateData.TimeOfDayNormalized * 6.28318530718f);
  const float day_factor = Clamp01(solar_phase * 0.5f + 0.5f);
  EnvironmentStateData.DayNightFactor = day_factor;
  EnvironmentStateData.WeatherSkyAttenuation =
      std::clamp(1.0f - EnvironmentStateData.Cloudiness * 0.45f, 0.35f, 1.0f);

  const bool precip_active =
      EnvironmentStateData.PrecipitationIntensity > 0.05f &&
      (EnvironmentStateData.Weather == WeatherType::Rain ||
       EnvironmentStateData.TargetWeather == WeatherType::Rain ||
       EnvironmentStateData.Weather == WeatherType::Storm ||
       EnvironmentStateData.TargetWeather == WeatherType::Storm ||
       EnvironmentStateData.Weather == WeatherType::Snow ||
       EnvironmentStateData.TargetWeather == WeatherType::Snow);
  const float target_wetness =
      precip_active ? EnvironmentStateData.PrecipitationIntensity * 0.85f
                    : 0.0f;
  const float wet_lerp =
      std::clamp(dtSeconds * (precip_active ? 0.35f : 0.12f), 0.0f, 1.0f);
  EnvironmentStateData.SurfaceWetness +=
      (target_wetness - EnvironmentStateData.SurfaceWetness) * wet_lerp;
}

void UWorld::RebuildAllLightingDirtyMeshes() { InvalidateBlockMesh(); }

bool UWorld::HasPersistedTerrainOnDisk(const std::string &world_folder_path)
{
  return UWorldPersistence::HasPersistedTerrainOnDisk(world_folder_path);
}

UChunkStorageService &UWorld::GetChunkStorage()
{
  return Persistence->GetChunkStorage();
}

const UChunkStorageService &UWorld::GetChunkStorage() const
{
  return Persistence->GetChunkStorage();
}

const std::string &UWorld::GetWorldFolderPath() const
{
  return Persistence->GetWorldFolderPath();
}

void UWorld::SetWorldFolderPath(const std::string &path)
{
  Persistence->SetWorldFolderPath(path);
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
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
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
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
  }
  if (rebuildPipeline)
  {
    RebuildWorldGenPipeline();
  }
}

void UWorld::SetWorldGenSets(WorldGenSets sets)
{
  WorldGenSetsData = std::move(sets);
  RebuildResolvedObjectFeatures();
  RebuildWorldGenPipeline();
}

void UWorld::SaveWorldGenSetsToDisk()
{
  if (GetWorldFolderPath().empty())
  {
    return;
  }
  Persistence->SaveWorldData(*this, GetWorldFolderPath() + "/world_data.json");
}

void UWorld::RebuildResolvedObjectFeatures()
{
  if (UObjectFeatureConfigStorage::IsLoaded())
  {
    ResolvedObjectFeatures = ResolveObjectFeatures(
        WorldGenSetsData, UObjectFeatureConfigStorage::Get());
  }
}

void UWorld::RebuildWorldGenPipeline()
{
  if (!BlockRegistry)
  {
    WorldGen.reset();
    return;
  }
  RebuildResolvedObjectFeatures();
  WorldGenContext ctx{BlockWorld, *BlockRegistry, ProceduralTemplate,
                      ObjectLibrary};
  ctx.WorldgenOwnerPackId = WorldgenOwnerPackId;
  ctx.ObjectFeatures = &ResolvedObjectFeatures;
  ctx.OnColumnMeshDirty = [this](int world_x, int world_z, int min_y, int max_y)
  { MarkColumnMeshDirty(world_x, world_z, min_y, max_y); };
  WorldGen = UProceduralWorldGenFactory::Create(ctx);
}

void UWorld::SetRenderDistanceChunks(int distance)
{
  RenderDistanceChunks = distance;
  EffectiveRenderDistance = distance;
  if (Streaming->HasStreamer())
  {
    Streaming->SetRenderDistance(distance);
  }
  MeshService->SetRenderDistanceChunks(distance);
}

void UWorld::SetChunkWriteFormat(ChunkWriteFormat format)
{
  Persistence->SetChunkWriteFormat(format);
}

ChunkWriteFormat UWorld::GetChunkWriteFormat() const
{
  return Persistence->GetChunkWriteFormat();
}

void UWorld::InitStreamerCallbacks()
{
  Streaming->InitStreamerCallbacks(*this);
}

void UWorld::TickAsyncChunkSystems()
{
  Streaming->TickAsyncChunkSystems(*this);
}

void UWorld::TickMeshEmerge()
{
  if (!BlockRegistry)
  {
    return;
  }
  Streaming->TickMeshEmerge(*this);
}

void UWorld::RefreshStreamerSettings()
{
  Streaming->RefreshStreamerSettings(ProceduralTemplate, MaxLoadOpsPerFrame,
                                     MaxUnloadOpsPerFrame);
}

void UWorld::SetStreamingEnabled(bool enabled)
{
  Streaming->SetStreamingEnabled(enabled);
}

bool UWorld::IsStreamingEnabled() const
{
  return Streaming->IsStreamingEnabled();
}

void UWorld::LoadInitialStreamingChunks()
{
  Persistence->LoadInitialTerrainColumns(*this, SpawnPoint,
                                         RenderDistanceChunks);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorld.GetChunkManager().ForEachChunk(
      [this](const UChunk &chunk)
      { MeshService->MarkDirty(chunk.GetCoord()); });
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
  if (IsStreamingEnabled())
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
  else if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
    }
    Collision.SetBlockRegistry(BlockRegistry.get());
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

void UWorld::OnBlockRegistryRuntimeOverlayChanged(
    const RuntimeOverlayFlushResult *flush)
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  std::vector<BlockId> affected_ids;
  if (flush)
  {
    affected_ids = flush->RemovedBlockIds;
    affected_ids.reserve(affected_ids.size() +
                         flush->PatchedDescriptors.size());
    for (const MergedCubeDesc &desc : flush->PatchedDescriptors)
    {
      affected_ids.push_back(desc.Id);
    }
  }
  WorldMeshDirtyPolicy::MarkRuntimeOverlayMeshDirty(*this, BlockWorld,
                                                    *MeshService, affected_ids);
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
  Environment.ReloadAllCreatureVisuals();
}

void UWorld::WaitForPendingMeshJobs() { MeshService->WaitForAsyncMeshIdle(); }

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
      BlockWorld.SetFluidDefinitions(BlockDefinitions.get());
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
  MeshService->RebuildAll(BlockWorld, *BlockRegistry);
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

std::optional<int> UWorld::FindHighestSolidY(int x, int z) const
{
  return Collision.FindHighestSolidY(x, z);
}

std::optional<float> UWorld::QueryGroundFeetYColumn(int worldX,
                                                    int worldZ) const
{
  return Collision.QueryGroundFeetYColumn(worldX, worldZ);
}

std::optional<float> UWorld::QueryGroundFeetYUnder(int worldX, int worldZ,
                                                   float referenceFeetY) const
{
  return Collision.QueryGroundFeetYUnder(worldX, worldZ, referenceFeetY);
}

bool UWorld::IsValidStandCell(const glm::ivec3 &cell,
                              const PlayerCapsule &cap) const
{
  return Collision.IsValidStandCell(cell, cap);
}

bool UWorld::IsValidStandFootprint(const glm::vec3 &eyePos,
                                   const PlayerCapsule &cap, float feetY) const
{
  return Collision.IsValidStandFootprint(eyePos, cap, feetY);
}

void UWorld::WarmupSpawnAreaForEnterGame()
{
  Streaming->WarmupSpawnAreaForEnterGame(*this);
}

void UWorld::ResetPhysicsRuntimeState()
{
  if (BlockPhysicsService)
  {
    BlockPhysicsService->ResetRuntimeState();
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->ClearPendingQueues();
  }
}

void UWorld::FinalizePlayerAfterWorldLoad()
{
  ResetPhysicsRuntimeState();
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
  if (Environment.GetControlledCreatureId() == 0)
  {
    if (Environment.GetPlayerCreatureId() != 0)
    {
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
    else if (auto user = GetCurrentUser())
    {
      if (user->GetPlayerCreatureId() != 0)
      {
        Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
        SetControlledCreature(Environment.GetPlayerCreatureId());
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

  if (ViewBinding)
  {
    ViewBinding->ResetCurrentCameraVerticalPhysics(*this);
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

bool UWorld::TickCooperativeLoad(IUProgressSink &sink, int chunkBudget)
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

bool UWorld::TickCooperativeSave(IUProgressSink &sink, int chunkBudget)
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

bool UWorld::TickCooperativeCreate(IUProgressSink &sink, int columnBudget)
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

bool UWorld::TryAddFluidObject(glm::ivec3 blockPos, BlockId liquidId)
{
  return UWorldFluidFacade::TryAddFluidObject(*this, blockPos, liquidId);
}

void UWorld::ApplyBreakSiteFluidFlood(
    glm::ivec3 blockPos, std::vector<glm::ivec3> &mesh_touch_blocks)
{
  UWorldFluidFacade::ApplyBreakSiteFluidFlood(*this, blockPos,
                                              mesh_touch_blocks);
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
  const BlockId existing = BlockWorld.GetBlock(blockPos);
  if (BlockRegistry->IsLiquid(Id))
  {
    return TryAddFluidObject(blockPos, Id);
  }
  else if (existing != BLOCK_AIR && !BlockRegistry->IsLiquid(existing))
  {
    if (BlockRegistry->BlocksMovement(existing))
    {
      return false;
    }
  }
  BlockWorld.SetBlock(blockPos, Id);
  if (BlockWorld.GetBlock(blockPos) != Id)
  {
    return false;
  }
  ++CachedBlockCount;
  BlockWorldReady = true;
  MarkBlockChunkDirty(blockPos);
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  if (BlockRegistry && BlockRegistry->IsLiquid(Id) && PhysicsFlags.EnableFluids)
  {
    EnqueueFluidFrontierAt(*this, blockPos);
    MarkBlockChunkDirty(blockPos);
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      MarkBlockChunkDirty(blockPos + offset);
    }
  }
  return true;
}

bool UWorld::PlaceObject(const std::string &prefab_name,
                         glm::ivec3 anchorWorldPos)
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }

  const ObjectPlacementStats stats =
      PlaceObjectAt(BlockWorld, *BlockRegistry, *prefab, anchorWorldPos, true);
  if (stats.placedCount == 0)
  {
    return false;
  }
  for (const auto &voxel : prefab->voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab->anchor;
    const BlockId blockId =
        ResolveObjectVoxelPlacementId(voxel, *BlockRegistry);
    if (BlockWorld.GetBlock(worldPos) == blockId)
    {
      MarkBlockChunkDirty(worldPos);
    }
  }
  return true;
}

bool UWorld::CanPlaceObject(const std::string &prefab_name,
                            glm::ivec3 anchorWorldPos) const
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }
  return CanPlaceObjectAt(BlockWorld, *prefab, anchorWorldPos);
}

std::optional<glm::ivec3>
UWorld::FindObjectAnchorFromView(const glm::vec3 &position,
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
  if (const auto &creature_definitions = GetCreatureDefinitionStorage())
  {
    const std::string controlled =
        creature_definitions->GetControlledDefaultSpeciesId();
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
    Environment.SetPlayerCreatureId(pid);
    Environment.SetControlledCreatureId(pid);
  }
  if (!ViewBinding)
  {
    return false;
  }
  const size_t viewId = ViewBinding->CreateUserCamera(SpawnPoint);
  user->SetViewId(viewId);
  if (Users.size() == 1)
  {
    SetCurrentUserName(Name);
    ViewBinding->SetActiveCamera(viewId);
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
      Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
  }
  ApplyUserToCamera(GetCurrentUser());
  if (auto user = GetCurrentUser(); user && ViewBinding)
  {
    ViewBinding->SetActiveCamera(user->GetViewId());
  }
  return true;
}

bool UWorld::CheckRayIntersection(
    const glm::vec3 &position, const glm::vec3 &front,
    std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
        &distance_map) const
{
  return Collision.CheckRayIntersection(position, front, distance_map);
}

bool UWorld::CheckRayIntersection(const glm::vec3 &position,
                                  const glm::vec3 &front,
                                  glm::vec3 &intersecion, float &distance,
                                  size_t &cube_index, int &cube_side,
                                  size_t &object_index) const
{
  return Collision.CheckRayIntersection(position, front, intersecion, distance,
                                        cube_index, cube_side, object_index);
}

bool UWorld::CheckPositionFree(const glm::vec3 &position, float size) const
{
  return Collision.CheckPositionFree(position, size);
}

std::optional<glm::vec3>
UWorld::FindNearestFreeCubePosition(const glm::vec3 &position,
                                    const glm::vec3 &front,
                                    const PlayerCapsule &cap) const
{
  return Collision.FindNearestFreeCubePosition(position, front, cap);
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

  PlayerCapsule cap = ViewBinding ? ViewBinding->ResolvePlacementCapsule(*this)
                                  : PlayerCapsule::Standing();

  const BlockPlacementResolve resolved =
      Collision.ResolveBlockPlacement(position, front, cap, 8.0f);
  if (!resolved.place_block_pos)
  {
    return false;
  }
  if (AddObject(blockType, BlockCenter(*resolved.place_block_pos)))
  {
    UpdateIntersection(position, front);
    return true;
  }
  return false;
}

bool UWorld::PlaceActiveObjectByView(const glm::vec3 &position,
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
      controlled->GetInventory().GetActiveObjectName();
  if (prefabName.empty())
  {
    return false;
  }

  const auto anchor = FindObjectAnchorFromView(position, front);
  if (!anchor.has_value())
  {
    return false;
  }
  if (PlaceObject(prefabName, anchor.value()))
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
  const std::vector<glm::ivec3> broken_above =
      BreakUnsupportedBlocksAbove(BlockWorld, *BlockRegistry, blockPos);
  std::vector<glm::ivec3> mesh_touch_blocks;
  mesh_touch_blocks.reserve(1 + broken_above.size());
  mesh_touch_blocks.push_back(blockPos);
  mesh_touch_blocks.insert(mesh_touch_blocks.end(), broken_above.begin(),
                           broken_above.end());
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  for (const glm::ivec3 &above_pos : broken_above)
  {
    if (CachedBlockCount > 0)
    {
      --CachedBlockCount;
    }
    PublishBlockPhysicsEvent(above_pos);
    PublishNeighborPhysicsEvents(above_pos);
  }
  ApplyBreakSiteFluidFlood(blockPos, mesh_touch_blocks);
  MarkBlocksChunkDirtyBatch(mesh_touch_blocks);
  if (ViewBinding)
  {
    ViewBinding->RefreshIntersectionFromCurrentView(*this);
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

FluidColumnSurface UWorld::FindFluidColumnSurfaceAt(int bx, int bz,
                                                    int hintY) const
{
  return Collision.FindFluidColumnSurfaceAt(bx, bz, hintY);
}

FluidColumnSurface UWorld::FindFluidColumnSurface(const glm::vec3 &eye) const
{
  return Collision.FindFluidColumnSurfaceEye(eye);
}

bool UWorld::HasNearbyFluidSurface(glm::ivec3 cameraBlock,
                                   int radiusBlocks) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  return HasFluidSurfaceNear(BlockWorld, *BlockRegistry, cameraBlock.x,
                             cameraBlock.z, cameraBlock.y, radiusBlocks);
}

bool UWorld::IsCameraInsideFluid(const glm::vec3 &eye, BlockId *outFluid) const
{
  const FluidColumnSurface column = FindFluidColumnSurface(eye);
  if (!column.valid || eye.y >= column.surfaceY)
  {
    return false;
  }
  if (outFluid)
  {
    *outFluid = column.fluidId;
  }
  return true;
}

SampledFluidState
UWorld::SampleFluidPhysicsVolume(const CollisionVolume &vol) const
{
  return Collision.SampleFluidPhysicsVolume(vol);
}

bool UWorld::IsFoliageFluidBlock(BlockId id) const
{
  if (!BlockRegistry || id == BLOCK_AIR)
  {
    return false;
  }
  const auto &mov = BlockRegistry->Physics(id).Movement;
  return mov.Occupancy < 1.0f && mov.SinkSpeed == 0.0f &&
         mov.RiseSpeed == 0.0f && mov.DragHorizontal == 0.0f;
}

SampledFluidState UWorld::SampleFluidPhysics(const glm::vec3 &eyePos,
                                             const PlayerCapsule &cap) const
{
  return SampleFluidPhysicsVolume(CollisionVolumeFromEye(eyePos, cap));
}

bool UWorld::CheckBlockCollisionVolume(const CollisionVolume &vol) const
{
  return Collision.CheckBlockCollisionVolume(vol);
}

bool UWorld::CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                          CreatureId skipCreatureId) const
{
  return Collision.CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollisionVolume(const CollisionVolume &vol,
                                  CreatureId skipCreatureId) const
{
  return Collision.CheckCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const
{
  return CheckCollision(eyePos, cap, GetMovementCollisionSkipId());
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.CheckCollision(eyePos, cap, skipCreatureId);
}

bool UWorld::DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.DepenetrateEye(eyePos, cap, skipCreatureId);
}

bool UWorld::HasGroundSupportVolume(const CollisionVolume &vol,
                                    float feetY) const
{
  return Collision.HasGroundSupportVolume(vol, feetY);
}

bool UWorld::HasGroundSupport(const glm::vec3 &eyePos,
                              const PlayerCapsule &cap) const
{
  return Collision.HasGroundSupport(eyePos, cap);
}

glm::vec3 UWorld::ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                      const glm::vec3 &delta,
                                      const glm::vec3 &currentSizeBlocks,
                                      CreatureId skipCreatureId) const
{
  return Collision.ResolveMovementBody(bodyOrigin, delta, currentSizeBlocks,
                                       skipCreatureId);
}

glm::vec3 UWorld::ResolveMovement(const glm::vec3 &eyePos,
                                  const glm::vec3 &delta,
                                  const PlayerCapsule &cap,
                                  CreatureId skipCreatureId) const
{
  return Collision.ResolveMovement(eyePos, delta, cap, skipCreatureId);
}

UWorldCollision::StepUpProbe UWorld::ProbeStepUp(const glm::vec3 &eyePos,
                                                 const glm::vec3 &horiz,
                                                 const PlayerCapsule &cap,
                                                 float maxTriggerDistance) const
{
  return Collision.ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

bool UWorld::GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                              const PlayerCapsule &cap,
                              float maxTriggerDistance,
                              glm::vec3 &outLanding) const
{
  return Collision.GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance,
                                    outLanding);
}

bool UWorld::TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                       const PlayerCapsule &cap, float maxTriggerDistance) const
{
  return Collision.TryStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

void UWorld::LoadUsers(const std::string &file_name)
{
  Persistence->LoadUsers(*this, file_name);
}

void UWorld::SaveUsers(const std::string &file_name)
{
  Persistence->SaveUsers(*this, file_name);
}

void UWorld::LoadWorldData(const std::string &file_name)
{
  Persistence->LoadWorldData(*this, file_name);
}

void UWorld::SaveWorldData(const std::string &file_name)
{
  Persistence->SaveWorldData(*this, file_name);
}

void UWorld::SaveMovementDiagnostics(const std::string &file_name) const
{
  UMovementDiagnosticsRecorder::SaveToFile(*this, file_name);
}

void UWorld::ConfigurePhysicsServices()
{
  BlockPhysicsService = std::make_unique<UWorldBlockPhysicsService>();
  MovementPhysicsService = std::make_unique<UWorldMovementPhysicsService>();
  ChunkDirtyService = std::make_unique<UWorldChunkDirtyService>();
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(PhysicsBudgetConfig);
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(PhysicsBudgetConfig);
  }
  PhysicsScheduler = std::make_unique<UWorldPhysicsScheduler>(
      MovementPhysicsService.get(), BlockPhysicsService.get(),
      ChunkDirtyService.get());
  Collision.SetTelemetry(&PhysicsTelemetryData);
}

void UWorld::SetPhysicsProfile(PhysicsProfile profile)
{
  ActivePhysicsProfile = profile;
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsFeatureFlags(const PhysicsFeatureFlags &flags)
{
  PhysicsFlags = flags;
  Collision.SetBroadphaseEnabled(flags.EnableCollisionBroadphase);
  Collision.SetCollisionDdaEnabled(flags.EnableCollisionDda);
  Collision.SetTelemetry(&PhysicsTelemetryData);
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsBudgets(const PhysicsBudgets &budgets)
{
  PhysicsBudgetConfig = budgets;
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(budgets);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(budgets);
  }
}

void UWorld::UpdatePhysicsQueueStats(const BlockUpdateQueueStats &blockStats,
                                     const FluidUpdateSetStats &fluidStats)
{
  PhysicsTelemetryData.BlockQueueDepth = blockStats.Depth;
  PhysicsTelemetryData.LiquidQueueDepth = fluidStats.Depth;
  PhysicsTelemetryData.DeferredUpdates = blockStats.Deferred;
  PhysicsTelemetryData.DroppedUpdates = blockStats.Dropped + fluidStats.Dropped;
  PhysicsTelemetryData.PurgedUpdates = blockStats.Purged;
}

void UWorld::AccumulateFallingStats(const FallingBlocksStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Deferred;
  PhysicsTelemetryData.DroppedUpdates += stats.Dropped;
}

void UWorld::AccumulateFluidStats(const FluidSpreadStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Candidates - stats.Applied;
  PhysicsTelemetryData.DroppedUpdates += 0;
  (void)stats;
}

bool UWorld::IsWithinLiquidUpdateRadius(glm::ivec3 blockPos) const
{
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  const glm::ivec3 focus = MovementDiag.feetChunk;
  const int radius = std::max({std::abs(chunkCoord.x - focus.x),
                               std::abs(chunkCoord.y - focus.y),
                               std::abs(chunkCoord.z - focus.z)});
  return radius <= PhysicsBudgetConfig.LiquidUpdateRadiusChunks;
}

void UWorld::MarkFluidRegionDirty(glm::ivec3 center, int block_radius)
{
  UWorldFluidFacade::MarkFluidRegionDirty(*this, center, block_radius);
}

void UWorld::MarkFluidChangeDirty(glm::ivec3 blockPos)
{
  // Simulation path: always budgeted remesh (async when enabled). Use
  // MarkBlockChunkDirty for player-driven edits that need instant feedback.
  MarkBlockChunkDirtyFromPhysics(blockPos);
}

void UWorld::MarkFluidFloodMeshDirty(
    glm::ivec3 blockPos, const std::vector<glm::ivec3> &filled_blocks)
{
  UWorldFluidFacade::MarkFluidFloodMeshDirty(*this, blockPos, filled_blocks);
}

void UWorld::TryEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  const UBlockDefinitionStorage *definitions = BlockRegistry->GetDefinitions();
  if (definitions == nullptr)
  {
    return;
  }
  const BlockId block_id = BlockWorld.GetBlock(blockPos);
  if (!BlockRegistry->IsLiquid(block_id) &&
      !UFluidSpreadSystem::CanReceiveFluid(BlockWorld, *definitions, blockPos))
  {
    return;
  }
  if (!UFluidSpreadSystem::HasSpreadTargetForTick(BlockWorld, *definitions,
                                                  blockPos, PhysicsTickCounter))
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
}

void UWorld::ForceEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
}

void UWorld::WakeFluidFrontier(glm::ivec3 blockPos, int radius_blocks)
{
  if (!BlockRegistry || !PhysicsFlags.EnableFluids || !BlockPhysicsService)
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
  const int clamped_radius = std::max(0, radius_blocks);
  for (int dx = -clamped_radius; dx <= clamped_radius; ++dx)
  {
    for (int dy = -clamped_radius; dy <= clamped_radius; ++dy)
    {
      for (int dz = -clamped_radius; dz <= clamped_radius; ++dz)
      {
        if (dx == 0 && dy == 0 && dz == 0)
        {
          continue;
        }
        const glm::ivec3 pos(blockPos.x + dx, blockPos.y + dy, blockPos.z + dz);
        if (BlockRegistry->IsLiquid(BlockWorld.GetBlock(pos)))
        {
          TryEnqueueFluidAt(pos);
        }
      }
    }
  }
}

void UWorld::TrySeedFallingAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFalling)
  {
    return;
  }
  if (!BlockRegistry->IsFallingBlock(BlockWorld.GetBlock(blockPos)))
  {
    return;
  }
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishSupportLost(
      blockPos, chunk_coord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
}

void UWorld::PublishBlockPhysicsEvent(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishBlockChanged(
      blockPos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
}

void UWorld::PublishNeighborPhysicsEvents(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 pos = blockPos + offset;
    const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(pos);
    BlockPhysicsService->PublishNeighborChanged(
        pos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
  }
  const glm::ivec3 above(blockPos.x, blockPos.y + 1, blockPos.z);
  const glm::ivec3 aboveChunk = UChunkManager::WorldToChunk(above);
  BlockPhysicsService->PublishSupportLost(above, aboveChunk, PhysicsTickCounter,
                                          ++PhysicsEventOrderCounter);
}

bool UWorld::IsCollisionReadyAtFeet(const glm::ivec3 &feetBlock) const
{
  if (!PhysicsFlags.EnableCollisionReadinessGate || !Streaming ||
      !Streaming->HasStreamer())
  {
    return true;
  }
  if (const auto *streamer = Streaming->GetStreamer())
  {
    return streamer->IsCollisionReady(
        feetBlock, PhysicsBudgetConfig.CollisionSafetyRadiusChunks);
  }
  return false;
}

void UWorld::DoMovement()
{
  TickEnvironment(static_cast<float>(WallFrameDeltaSec));
  ++PhysicsTickCounter;
  if (PhysicsScheduler)
  {
    using clock = std::chrono::high_resolution_clock;
    const auto t_begin = clock::now();
    auto t_after_move = t_begin;

    if (MovementPhysicsService)
    {
      MovementPhysicsService->TickMovement(*this);
      t_after_move = clock::now();
    }

    double block_ms = 0.0;
    double drain_ms = 0.0;
    if (BlockPhysicsService)
    {
      const auto tb = clock::now();
      BlockPhysicsService->TickBlockPhysics(*this);
      block_ms =
          std::chrono::duration<double, std::milli>(clock::now() - tb).count();
    }
    if (ChunkDirtyService)
    {
      const auto tb = clock::now();
      ChunkDirtyService->DrainRebuildQueues(*this);
      drain_ms =
          std::chrono::duration<double, std::milli>(clock::now() - tb).count();
    }

    const auto t_end = clock::now();
    PhysicsTelemetryData.MovementStepMs =
        std::chrono::duration<double, std::milli>(t_after_move - t_begin)
            .count();
    PhysicsTelemetryData.BlockStepMs = block_ms;
    PhysicsTelemetryData.DrainStepMs = drain_ms;
    PhysicsTelemetryData.FluidStepMs = block_ms;
    PhysicsTelemetryData.SimulationStepsThisFrame = 1;
    PhysicsTelemetryData.PhysicsStepMs =
        std::chrono::duration<double, std::milli>(t_end - t_begin).count();
    DurationDoMovementMks = static_cast<uint64_t>(
        std::chrono::duration<double, std::micro>(t_end - t_begin).count());
    return;
  }
  RunLegacyPhysicsFrame();
}

void UWorld::SetWallFrameDelta(double seconds)
{
  WallFrameDeltaSec = seconds > 0.0 ? seconds : 0.0;
}

void UWorld::UpdateFrameHitchDiagnostics(double draw_scene_mks,
                                         double view_update_mks)
{
  DurationDrawSceneMks = static_cast<uint64_t>(draw_scene_mks);
  const double sim_ms =
      (DurationDoMovementMks + view_update_mks + draw_scene_mks) / 1000.0;
  const double wall_ms =
      WallFrameDeltaSec > 0.0 ? WallFrameDeltaSec * 1000.0 : sim_ms;
  const double frameMs = std::max(sim_ms, wall_ms);
  MovementDiag.hitchDetected = frameMs > 50.0 ||
                               PhysicsTelemetryData.PhysicsStepMs > 50.0 ||
                               MovementDiag.deltaTime > 0.1f;
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
  if (FramesSinceLoad < 600 || MeshService->HasPendingDirty() ||
      MeshService->HasPendingAsyncMeshWork())
  {
    ++FramesSinceLoad;
  }
  if (!MeshBacklogClearedLatch && !MeshService->HasPendingDirty() &&
      !MeshService->HasPendingAsyncMeshWork())
  {
    MeshBacklogClearedLatch = true;
  }
  if (MeshBacklogClearedLatch && FramesSinceLoad >= 600 &&
      !MeshService->HasPendingDirty())
  {
    MeshLoadDiagActive = false;
  }
}

void UWorld::UpdateStreaming()
{
  Streaming->UpdateStreaming(*this, *MeshService, Render, RenderDistanceChunks,
                             EffectiveRenderDistance, EffectiveFogStartRatio,
                             AltitudeParams, LastCameraPosition,
                             LastMovementSpeed);
}

size_t UWorld::GetRenderInstanceCount() const
{
  if (Render.GreedyMeshing)
  {
    return MeshService->GetGreedyVertexCount();
  }
  return MeshService->GetInstanceCount();
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
    MeshService->MarkAllDirtyFromWorld(BlockWorld);
  }
}

void UWorld::SetRenderSettings(const RenderSettings &settings)
{
  Render = settings;
  MeshService->SetRenderSettings(settings);
}

UWorldMeshService &UWorld::GetMeshService() { return *MeshService; }

const UWorldMeshService &UWorld::GetMeshService() const { return *MeshService; }

void UWorld::MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y)
{
  MeshService->MarkColumnMeshDirty(world_x, world_z, min_y, max_y);
}

void UWorld::MarkTerrainChunkMeshDirty(glm::ivec3 groundChunkCoord, int min_y,
                                       int max_y)
{
  MeshService->MarkTerrainChunkMeshDirty(groundChunkCoord, min_y, max_y);
}

void UWorld::MarkBlocksChunkDirtyBatch(
    const std::vector<glm::ivec3> &block_positions)
{
  MeshService->MarkBlocksChunkDirtyBatchFromEdit(
      BlockWorld, BlockRegistry.get(), block_positions, ModifiedChunks);
}

void UWorld::MarkBlockChunkDirty(glm::ivec3 blockPos)
{
  MeshService->MarkBlockChunkDirtyFromEdit(BlockWorld, BlockRegistry.get(),
                                           blockPos, ModifiedChunks);
}

void UWorld::MarkBlockChunkDirtyFromPhysics(glm::ivec3 blockPos)
{
  if (ChunkDirtyService)
  {
    ChunkDirtyService->MarkVisualRemesh(*this, blockPos);
    ChunkDirtyService->MarkCollisionRebuild(*this, blockPos);
    return;
  }
  MarkBlockChunkDirty(blockPos);
}

} // namespace cutum
