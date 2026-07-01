// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "App/Core.h"
#include "App/LegacyConfigAdapter.h"
#include "App/Platform/GameDataRoot.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/Gltf/CreatureGltfCache.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Version.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/WorldGenPack.h"

using json = nlohmann::json;

namespace cutum
{

const UBlockDefinitionStorage &UCore::Blocks() const
{
  static const UBlockDefinitionStorage kEmpty;
  if (BlockDefinitionsInstance)
  {
    return *BlockDefinitionsInstance;
  }
  return kEmpty;
}

const UObjectLibrary &UCore::Objects() const
{
  static const UObjectLibrary kEmpty;
  if (ObjectLibraryInstance)
  {
    return *ObjectLibraryInstance;
  }
  return kEmpty;
}

const UCreatureDefinitionStorage &UCore::Creatures() const
{
  static const UCreatureDefinitionStorage kEmpty;
  if (WorldInstance)
  {
    const auto &storage = WorldInstance->GetCreatureDefinitionStorage();
    if (storage)
    {
      return *storage;
    }
  }
  return kEmpty;
}

const WorldGenPack &UCore::ActiveWorldGenPack() const
{
  return UWorldGenPack::Get();
}

std::filesystem::path GetExecutableDirectory()
{
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
  {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buffer).parent_path();
#else
  return std::filesystem::current_path();
#endif
}

UCore::UCore(std::shared_ptr<UTextureBaseStorage> texture_base_storage_,
             std::shared_ptr<UTextureCubeStorage> texture_cube_storage_,
             std::shared_ptr<UObjectLibrary> object_library_,
             std::shared_ptr<UWorld> World,
             std::shared_ptr<UGeometryEngine> Geometries,
             std::shared_ptr<UViewEngine> Views)
    : TextureBaseStorageInstance(texture_base_storage_),
      TextureCubeStorageInstance(texture_cube_storage_),
      ObjectLibraryInstance(object_library_), WorldInstance(World),
      GeometryEngineInstance(Geometries), ViewEngineInstance(Views)
{
}

UCore::~UCore()
{
  UnwireRenderMeshSink();
}

void UCore::WireRenderMeshSink()
{
  RenderMeshSink.Attach(GeometryEngineInstance.get());
  if (WorldInstance)
  {
    WorldInstance->GetMeshService().SetMeshSink(&RenderMeshSink);
  }
}

void UCore::UnwireRenderMeshSink()
{
  if (WorldInstance)
  {
    WorldInstance->GetMeshService().SetMeshSink(nullptr);
  }
  RenderMeshSink.Attach(nullptr);
}

std::filesystem::path
UCore::WorldFolderPath(const std::string &world_name) const
{
  return WorldPath / world_name;
}

bool UCore::ShouldCreateWorldOnStartup() const
{
  if (DefaultWorldName.empty())
  {
    return true;
  }
  if (!std::filesystem::exists(WorldPath) ||
      !std::filesystem::is_directory(WorldPath))
  {
    return true;
  }

  bool hasWorldFolder = false;
  for (const auto &entry : std::filesystem::directory_iterator(WorldPath))
  {
    if (entry.is_directory())
    {
      hasWorldFolder = true;
      break;
    }
  }
  if (!hasWorldFolder)
  {
    return true;
  }

  const auto worldFolder = WorldFolderPath(DefaultWorldName);
  return !std::filesystem::exists(worldFolder);
}

void UCore::LoadConfig(const std::string &config_file_name)
{
#ifdef __ANDROID__
  if (auto *paths = IUPlatformPaths::TryGet())
  {
    ExeDir = paths->WritableRoot();
    ConfigFilePath = std::filesystem::path(config_file_name);
    WorldPath = paths->ResolveWritable("worlds");
    WorkDir = paths->AssetRoot();
    std::error_code pathEc;
    std::filesystem::current_path(WorkDir, pathEc);
    std::filesystem::create_directories(WorldPath);
  }
  else
#endif
  {
    ExeDir = GetExecutableDirectory();
    const auto cwd = std::filesystem::current_path();
    std::filesystem::path project_dir = cwd;
    if (auto fromExe = TryFindProjectRoot(ExeDir))
    {
      project_dir = *fromExe;
    }
    else if (auto fromCwd = TryFindProjectRoot(cwd))
    {
      project_dir = *fromCwd;
    }
    else
    {
      project_dir = FindProjectRoot(cwd);
    }
    std::error_code pathEc;
    std::filesystem::current_path(project_dir, pathEc);

    ConfigFilePath = ExeDir / config_file_name;
    WorldPath = ExeDir / "worlds";
    WorkDir = project_dir;
  }

  std::string val;
  bool configRead = false;
  std::ifstream file(ConfigFilePath.string());
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
    configRead = true;
  }
  else
  {
    std::cout << "Config not found, will create: " << ConfigFilePath.string()
              << std::endl;
  }

  try
  {
    if (configRead)
    {
      json d = json::parse(val);
      DefaultWorldName = d.value("default_world", "");
      DefaultUserName = d.value("default_user", "");
      WorldSeed = d.value("world_seed", 12345u);
      ProceduralTemplate = ParseProceduralTemplateFromConfig(d);
      WorldSeed = ProceduralTemplate.Seed;
      TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
      RenderDistanceChunks = d.value("render_distance_chunks", 4);
      StreamingEnabled = d.value("streaming_enabled", true);
      if (d.contains("chunk_storage") && d["chunk_storage"].is_string())
      {
        ChunkStorageFormat = d["chunk_storage"].get<std::string>();
      }
      else
      {
        ChunkStorageFormat = "binary";
      }
      if (d.contains("gameplay") && d["gameplay"].is_object())
      {
        const json &gameplay = d["gameplay"];
        StepUpEnabled = gameplay.value("step_up", true);
        EntityCollisionEnabled = gameplay.value("entity_collision", true);
      }
      else
      {
        StepUpEnabled = true;
        EntityCollisionEnabled = true;
      }
      if (d.contains("render") && d["render"].is_object())
      {
        const json &r = d["render"];
        if (r.contains("performance_preset") && r["performance_preset"].is_string())
        {
          const std::string preset = r["performance_preset"].get<std::string>();
          if (preset == "fast")
          {
            Render = RenderSettings::FromPreset(PerformancePreset::Fast);
          }
          else if (preset == "quality")
          {
            Render = RenderSettings::FromPreset(PerformancePreset::Quality);
          }
          else
          {
            Render = RenderSettings::FromPreset(PerformancePreset::Balanced);
          }
        }
        else
        {
          Render = RenderSettings::Default();
        }
        Render.GreedyMeshing = r.value("greedy_meshing", Render.GreedyMeshing);
        Render.AsyncMeshing = r.value("async_meshing", Render.AsyncMeshing);
        Render.FaceQuads = r.value("face_quads", Render.FaceQuads);
        Render.FrustumCulling = r.value("frustum_culling", Render.FrustumCulling);
        Render.BatchCache = r.value("batch_cache", Render.BatchCache);
        Render.CreatureDebugBounds =
            r.value("creature_debug_bounds", Render.CreatureDebugBounds);
        Render.CreatureTexturedParts =
            r.value("creature_textured_parts", Render.CreatureTexturedParts);
        Render.CreatureWireframeOverlay =
            r.value("creature_wireframe_overlay", Render.CreatureWireframeOverlay);
        Render.DistanceFog = r.value("distance_fog", Render.DistanceFog);
        Render.DistanceFogStartRatio =
            r.value("distance_fog_start_ratio", Render.DistanceFogStartRatio);
        Render.DistanceFogDensity =
            r.value("distance_fog_density", Render.DistanceFogDensity);
        Render.DistanceFogHorizontal =
            r.value("distance_fog_horizontal", Render.DistanceFogHorizontal);
        Render.AltitudeAdaptiveFog =
            r.value("altitude_adaptive_fog", Render.AltitudeAdaptiveFog);
        Render.AltitudeFogThresholdBlocks = r.value("altitude_fog_threshold_blocks",
                                                   Render.AltitudeFogThresholdBlocks);
        Render.AltitudeFogPenaltyPer16Blocks =
            r.value("altitude_fog_penalty_per_16_blocks",
                    Render.AltitudeFogPenaltyPer16Blocks);
        Render.GradientSky = r.value("gradient_sky", Render.GradientSky);
        if (Render.GreedyMeshing && !Render.FaceQuads)
        {
          std::cout
              << "Render: greedy_meshing enabled — auto-enabling face_quads"
              << std::endl;
          Render.FaceQuads = true;
        }
      }
      else
      {
        Render = RenderSettings::Default();
      }
      if (d.contains("ui") && d["ui"].is_object())
      {
        ReadLegacyUiSettings(d["ui"], Ui);
      }
      ResourcePacks = UResourcePackResolver::ParseFromJson(d);
    }
    else
    {
      DefaultWorldName.clear();
      DefaultUserName = "Username";
      WorldSeed = 12345u;
      TerrainType = "heightmap";
      ProceduralTemplate = ProceduralSettings{};
      ProceduralTemplate.Seed = WorldSeed;
      ResetToGeneratorDefaults(ProceduralTemplate);
      RenderDistanceChunks = 4;
      StreamingEnabled = true;
      Render = RenderSettings::Default();
      ResourcePacks = ResourcePacksConfig{};
      {
        const ResourcePackSelection defaults = DefaultResourcePackSelection();
        ResourcePacks.DefaultPrimary = defaults.Primary;
        ResourcePacks.DefaultSecondary = defaults.Secondary;
        ResourcePacks.DefaultEnabled = defaults.AllIds();
      }
    }

    TextureBaseStorageFileName = WorkDir / "textures" / "blocks";
    TextureCubeStorageFileName = WorkDir / "models" / "blocks";
    ObjectsPath = WorkDir / "objects";

    BlockDefinitionsInstance = std::make_shared<UBlockDefinitionStorage>();
    BlockMergeRegistryInstance = std::make_shared<UBlockMergeRegistry>();
    auto blockDefinitions = BlockDefinitionsInstance;

    ResourcePackBootstrap.InitPlaceholderCache(*this);

    WorldInstance->SetBlockMergeRegistry(BlockMergeRegistryInstance);
    WorldInstance->SetOnAfterWorldDataLoaded(
        [this]() { ResourcePackBootstrap.ApplyResourcePacksAfterWorldDataLoaded(*this); });

    WorldInstance->SetBlockDefinitionStorage(blockDefinitions);

    auto creatureDefinitions = std::make_shared<UCreatureDefinitionStorage>();
    creatureDefinitions->Load((WorkDir / "models" / "creatures").string());
    CreatureBoneSkeletonCache::Instance().SetCreaturesRoot(
        (WorkDir / "models" / "creatures").string());
    CreatureGltfCache::Instance().SetCreaturesRoot(
        (WorkDir / "models" / "creatures").string());
    WorldInstance->SetCreatureDefinitionStorage(creatureDefinitions);

    auto skinDefinitions = std::make_shared<USkinDefinitionStorage>();
    skinDefinitions->Load((WorkDir / "models" / "skins").string());
    WorldInstance->SetSkinDefinitionStorage(skinDefinitions);

    CreatureTextureStorageInstance =
        std::make_shared<UCreatureTextureStorage>();
    CreatureTextureStorageInstance->LoadFromCreatureAndSkinRoots(
        (WorkDir / "models" / "creatures").string(),
        (WorkDir / "models" / "skins").string());

    const ResourcePackSelection defaultPacks =
        GetDefaultResourcePackSelection();
    WorldInstance->SetResourcePackSelection(defaultPacks.Primary,
                                            defaultPacks.Secondary,
                                            defaultPacks.WorldgenOwner);
    WorldInstance->SetProceduralSettings(ProceduralTemplate, false);
    if (!ApplyResourcePacks(defaultPacks))
    {
      BlockMergeRegistryInstance->Rebuild({}, PlaceholderCacheInstance,
                                          ResourcePacks.PlaceholderTileSize);
      WorldInstance->RefreshBlockRegistry();
      if (ObjectLibraryInstance)
      {
        ObjectLibraryInstance->Load(ObjectsPath.string(),
                                    WorldInstance->GetBlockRegistry());
        WorldInstance->SetObjectLibrary(ObjectLibraryInstance.get());
      }
    }
    else if (auto user = WorldInstance->GetCurrentUser())
    {
      WorldInstance->EnsurePlayerHotbarCount(
          user, static_cast<size_t>(Ui.HotbarCount));
    }

    std::cout << "Procedural: "
              << ProceduralGeneratorToString(ProceduralTemplate.Generator)
              << " (Seed=" << ProceduralTemplate.Seed
              << ", sea=" << ProceduralTemplate.SeaLevel
              << ", maxY=" << ProceduralTemplate.MaxHeight
              << ", caves=" << (ProceduralTemplate.EnableCaves ? "1" : "0")
              << ", trees=" << (ProceduralTemplate.EnableTrees ? "1" : "0")
              << ")" << std::endl;
    WorldInstance->SetStreamingEnabled(StreamingEnabled);
    WorldInstance->SetRenderDistanceChunks(RenderDistanceChunks);
    WorldInstance->SetChunkWriteFormat(
        ChunkWriteFormatFromString(ChunkStorageFormat));
    WorldInstance->SetStepUpEnabled(StepUpEnabled);
    WorldInstance->SetEntityCollisionEnabled(EntityCollisionEnabled);
    WorldInstance->SetRenderSettings(Render);
    if (GeometryEngineInstance)
    {
      GeometryEngineInstance->SetRenderSettings(Render);
      GeometryEngineInstance->SetCreatureTextureStorage(
          CreatureTextureStorageInstance);
    }
    std::cout << "Render: greedy=" << Render.GreedyMeshing
              << " face_quads=" << Render.FaceQuads
              << " frustum=" << Render.FrustumCulling
              << " batch_cache=" << Render.BatchCache << std::endl;
    std::cout << "Gameplay: step_up=" << (StepUpEnabled ? "1" : "0")
              << " entity_collision=" << (EntityCollisionEnabled ? "1" : "0")
              << std::endl;
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error: " << e.what() << std::endl;
  }
}

void UCore::EnterGame()
{
  try
  {
    std::filesystem::create_directories(WorldPath);
    LoadWorldList(WorldPath.string());

    if (ShouldCreateWorldOnStartup())
    {
      if (!DefaultWorldName.empty())
      {
        std::cout << "Core::EnterGame: world '" << DefaultWorldName
                  << "' not found, creating a new one." << std::endl;
      }
      CreateWorld();
      SaveSystem(ConfigFilePath.filename().string());
    }
    else
    {
      LoadLastWorld();
    }

    if (DefaultUserName.empty())
    {
      DefaultUserName = WorldInstance->GetCurrentUserName();
    }
    if (WorldInstance->GetCurrentUser() == nullptr)
    {
      WorldInstance->GenerateUsers();
    }
    if (UCreature *player = WorldInstance->GetPlayerCreature())
    {
      if (player->GetInventory().GetActiveEntryRef() == nullptr)
      {
        player->GetInventory().SetActiveSlot(0, 1);
      }
    }
    std::cout << kCubatariumVersion << " (feet snap: BlockTopY)" << std::endl;
    WireRenderMeshSink();
  }
  catch (const std::exception &e)
  {
    std::cerr << "Core::EnterGame error: " << e.what() << std::endl;
    CreateWorld();
  }
}

void UCore::LoadSystem(const std::string &config_file_name)
{
  LoadConfig(config_file_name);
  EnterGame();
}

bool UCore::RegisterRuntimeBlock(const BlockDefinition &def,
                                 const std::array<std::string, 6> &textureStems)
{
  return ResourcePackBootstrap.RegisterRuntimeBlock(*this, def, textureStems);
}

void UCore::BeginRuntimeBlockBatch()
{
  ResourcePackBootstrap.BeginRuntimeBlockBatch(*this);
}

void UCore::EndRuntimeBlockBatch()
{
  ResourcePackBootstrap.EndRuntimeBlockBatch(*this);
}

void UCore::SaveConfigFile()
{
  if (ConfigFilePath.empty())
  {
    ConfigFilePath = ExeDir / "config.json";
  }

  json system_data;
  system_data["default_world"] = DefaultWorldName;
  system_data["default_user"] = DefaultUserName;
  system_data["world_seed"] = WorldSeed;
  WriteProceduralTemplateConfig(system_data, ProceduralTemplate);
  system_data["render_distance_chunks"] = RenderDistanceChunks;
  system_data["streaming_enabled"] = StreamingEnabled;
  system_data["chunk_storage"] = ChunkStorageFormat;
  json gameplay;
  gameplay["step_up"] = StepUpEnabled;
  gameplay["entity_collision"] = EntityCollisionEnabled;
  system_data["gameplay"] = gameplay;
  json render_json;
  const char *preset_name = "balanced";
  switch (Render.Preset)
  {
  case PerformancePreset::Fast:
    preset_name = "fast";
    break;
  case PerformancePreset::Quality:
    preset_name = "quality";
    break;
  case PerformancePreset::Balanced:
  default:
    preset_name = "balanced";
    break;
  }
  render_json["performance_preset"] = preset_name;
  render_json["greedy_meshing"] = Render.GreedyMeshing;
  render_json["async_meshing"] = Render.AsyncMeshing;
  render_json["face_quads"] = Render.FaceQuads;
  render_json["frustum_culling"] = Render.FrustumCulling;
  render_json["batch_cache"] = Render.BatchCache;
  render_json["creature_debug_bounds"] = Render.CreatureDebugBounds;
  render_json["creature_textured_parts"] = Render.CreatureTexturedParts;
  render_json["creature_wireframe_overlay"] = Render.CreatureWireframeOverlay;
  render_json["distance_fog"] = Render.DistanceFog;
  render_json["distance_fog_start_ratio"] = Render.DistanceFogStartRatio;
  render_json["distance_fog_density"] = Render.DistanceFogDensity;
  render_json["distance_fog_horizontal"] = Render.DistanceFogHorizontal;
  render_json["altitude_adaptive_fog"] = Render.AltitudeAdaptiveFog;
  render_json["altitude_fog_threshold_blocks"] =
      Render.AltitudeFogThresholdBlocks;
  render_json["altitude_fog_penalty_per_16_blocks"] =
      Render.AltitudeFogPenaltyPer16Blocks;
  render_json["gradient_sky"] = Render.GradientSky;
  system_data["render"] = render_json;
  WriteUiSettings(system_data, Ui);
  json resource_packs;
  resource_packs["default_primary"] = ResourcePacks.DefaultPrimary;
  resource_packs["default_secondary"] = ResourcePacks.DefaultSecondary;
  if (!ResourcePacks.DefaultEnabled.empty())
  {
    resource_packs["default_enabled"] = ResourcePacks.DefaultEnabled;
  }
  json placeholder;
  placeholder["tile_size"] = ResourcePacks.PlaceholderTileSize;
  placeholder["background"] = ResourcePacks.PlaceholderBackground;
  placeholder["max_entries"] = ResourcePacks.PlaceholderCacheMaxEntries;
  resource_packs["placeholder"] = placeholder;
  system_data["resource_packs"] = resource_packs;

  std::ofstream file(ConfigFilePath.string());
  if (file.is_open())
  {
    file << system_data.dump(4);
    file.close();
  }
  else
  {
    std::cerr << "Failed to write config: " << ConfigFilePath.string()
              << std::endl;
  }
}

void UCore::SaveSystem(const std::string &config_file_name)
{
  if (!WorldInstance->GetWorldName().empty())
  {
    DefaultWorldName = WorldInstance->GetWorldName();
  }
  if (!WorldInstance->GetCurrentUserName().empty())
  {
    DefaultUserName = WorldInstance->GetCurrentUserName();
  }
  WorldSeed = ProceduralTemplate.Seed;

  if (ConfigFilePath.empty())
  {
    ConfigFilePath = ExeDir / config_file_name;
  }

  SaveConfigFile();
  SaveWorld(WorldInstance->GetWorldName());
}

AppSettingsSnapshot UCore::GetAppSettings() const
{
  AppSettingsSnapshot snapshot;
  snapshot.DefaultUser = DefaultUserName;
  snapshot.DefaultWorld = DefaultWorldName;
  snapshot.RenderDistanceChunks = RenderDistanceChunks;
  snapshot.StreamingEnabled = StreamingEnabled;
  snapshot.StepUpEnabled = StepUpEnabled;
  snapshot.EntityCollisionEnabled = EntityCollisionEnabled;
  snapshot.Render = Render;
  snapshot.Ui = Ui;
  snapshot.DefaultResourcePacks = GetDefaultResourcePackSelection();
  snapshot.DefaultResourcePacksEnabled = snapshot.DefaultResourcePacks.AllIds();
  return snapshot;
}

void UCore::ApplyAppSettings(const AppSettingsSnapshot &settings)
{
  DefaultUserName = settings.DefaultUser;
  DefaultWorldName = settings.DefaultWorld;
  RenderDistanceChunks = settings.RenderDistanceChunks;
  StreamingEnabled = settings.StreamingEnabled;
  StepUpEnabled = settings.StepUpEnabled;
  EntityCollisionEnabled = settings.EntityCollisionEnabled;
  Render = settings.Render;
  Ui = settings.Ui;
  if (!settings.DefaultResourcePacks.Primary.empty())
  {
    SetDefaultResourcePackSelection(settings.DefaultResourcePacks);
  }
  else if (!settings.DefaultResourcePacksEnabled.empty())
  {
    SetDefaultEnabledResourcePacks(settings.DefaultResourcePacksEnabled);
  }

  WorldInstance->SetStreamingEnabled(StreamingEnabled);
  WorldInstance->SetRenderDistanceChunks(RenderDistanceChunks);
  WorldInstance->SetChunkWriteFormat(
      ChunkWriteFormatFromString(ChunkStorageFormat));
  WorldInstance->SetStepUpEnabled(StepUpEnabled);
  WorldInstance->SetEntityCollisionEnabled(EntityCollisionEnabled);
  WorldInstance->SetRenderSettings(Render);
  if (GeometryEngineInstance)
  {
    GeometryEngineInstance->SetRenderSettings(Render);
    GeometryEngineInstance->SetCreatureTextureStorage(
        CreatureTextureStorageInstance);
  }
}

void UCore::SetProceduralTemplate(const ProceduralSettings &settings)
{
  ProceduralTemplate.Generator = settings.Generator;
  ProceduralTemplate.Seed = settings.Seed;
  WorldSeed = settings.Seed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
}

void UCore::CreateNewWorldFromTemplate()
{
  WorldSeed += 1;
  ProceduralTemplate.Seed = WorldSeed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
  PendingNewWorldSettings.reset();
  WorldLifecycle.CreateNewWorldWithCurrentSettings(*this);
}

void UCore::RefreshWorldList()
{
  WorldLifecycle.RefreshWorldList(*this);
}

void UCore::LoadWorldByName(const std::string &world_name)
{
  WorldLifecycle.LoadWorldByName(*this, world_name);
}

void UCore::CreateWorld(const std::string &terrain_type)
{
  WorldLifecycle.CreateWorld(*this, terrain_type);
}

void UCore::CreateWorldFromProceduralConfig()
{
  WorldLifecycle.CreateWorldFromProceduralConfig(*this);
}

void UCore::PrepareStartupWorldCreation()
{
  WorldSeed += 1;
  ProceduralTemplate.Seed = WorldSeed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
  PendingNewWorldSettings.reset();
  PendingNewWorldPackSelection = GetDefaultResourcePackSelection();
}

void UCore::PrepareEnterGameWorldList()
{
  WorldLifecycle.PrepareEnterGameWorldList(*this);
}

bool UCore::NeedsCreateWorldOnStartup() const
{
  return ShouldCreateWorldOnStartup();
}

void UCore::PrepareLoadWorld(const std::string &world_name)
{
  WorldLifecycle.PrepareLoadWorld(*this, world_name);
}

void UCore::FinalizeLoadedWorld()
{
  WorldLifecycle.FinalizeLoadedWorld(*this);
}

void UCore::FinalizeEnterGameSession()
{
  WorldLifecycle.FinalizeEnterGameSession(*this);
}

void UCore::RefreshWorldListAfterSave()
{
  WorldLifecycle.RefreshWorldListAfterSave(*this);
}

std::string UCore::SetupNewWorldForCreation()
{
  return WorldLifecycle.SetupNewWorldForCreation(*this);
}

void UCore::ApplyRuntimeStreamingToWorld()
{
  if (!WorldInstance)
  {
    return;
  }
  ProceduralSettings worldSettings = WorldInstance->GetProceduralSettings();
  worldSettings.AsyncChunkGeneration = ProceduralTemplate.AsyncChunkGeneration;
  worldSettings.AsyncChunkIo = ProceduralTemplate.AsyncChunkIo;
  worldSettings.MaxChunkCommitsPerFrame =
      ProceduralTemplate.MaxChunkCommitsPerFrame;
  WorldInstance->SetProceduralSettings(worldSettings, false);
  WorldInstance->SetRenderSettings(Render);
  WorldInstance->RefreshStreamerSettings();
}

void UCore::LoadWorld(const std::string &world_name)
{
  WorldLifecycle.LoadWorld(*this, world_name);
}

void UCore::LoadLastWorld()
{
  WorldLifecycle.LoadLastWorld(*this);
}

void UCore::SaveWorld(const std::string &world_name)
{
  WorldLifecycle.SaveWorld(*this, world_name);
}

void UCore::LoadWorldList(const std::string &world_path)
{
  WorldLifecycle.LoadWorldList(*this, world_path);
}

std::vector<std::string> UCore::GetDefaultEnabledResourcePacks() const
{
  const auto selection = GetDefaultResourcePackSelection();
  return selection.AllIds();
}

ResourcePackSelection UCore::GetDefaultResourcePackSelection() const
{
  ResourcePackSelection selection;
  if (!ResourcePacks.DefaultPrimary.empty())
  {
    selection.Primary = ResourcePacks.DefaultPrimary;
    selection.Secondary = ResourcePacks.DefaultSecondary;
  }
  else if (!ResourcePacks.DefaultEnabled.empty())
  {
    selection.Primary = ResourcePacks.DefaultEnabled;
  }
  else
  {
    selection = DefaultResourcePackSelection();
  }
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  return selection;
}

void UCore::SetDefaultEnabledResourcePacks(const std::vector<std::string> &ids)
{
  ResourcePacks.DefaultEnabled = ids;
  ResourcePacks.DefaultPrimary = ids;
  ResourcePacks.DefaultSecondary.clear();
}

void UCore::SetDefaultResourcePackSelection(
    const ResourcePackSelection &selection)
{
  ResourcePacks.DefaultPrimary = selection.Primary;
  ResourcePacks.DefaultSecondary = selection.Secondary;
  ResourcePacks.DefaultEnabled = selection.AllIds();
}

std::vector<InstalledPackInfo> UCore::ListInstalledResourcePacks() const
{
  return UResourcePackResolver::ListInstalled(WorkDir, ExeDir);
}

bool UCore::ApplyResourcePacks(const std::vector<std::string> &enabledIds)
{
  return ResourcePackBootstrap.ApplyResourcePacks(*this, enabledIds);
}

bool UCore::ApplyResourcePacks(const ResourcePackSelection &selectionIn)
{
  return ResourcePackBootstrap.ApplyResourcePacks(*this, selectionIn);
}

void UCore::CreateNewWorldWithSettings(
    const ProceduralSettings &settings,
    const std::vector<std::string> &resourcePacks)
{
  WorldLifecycle.CreateNewWorldWithSettings(*this, settings, resourcePacks);
}

void UCore::CreateNewWorldWithSettings(
    const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  WorldLifecycle.CreateNewWorldWithSettings(*this, settings, resourcePacks);
}

void UCore::ApplyNewWorldCreationRequest(
    const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  WorldLifecycle.ApplyNewWorldCreationRequest(*this, settings, resourcePacks);
}

std::vector<std::string>
UCore::PeekWorldResourcePacks(const std::string &world_name) const
{
  const auto path = WorldFolderPath(world_name) / "world_data.json";
  std::ifstream file(path.string());
  if (!file.is_open())
  {
    return {};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  try
  {
    const json d = json::parse(buffer.str());
    return ReadLegacyWorldEnabledPacks(d);
  }
  catch (const json::exception &e)
  {
    std::cerr << "PeekWorldResourcePacks: " << e.what() << std::endl;
    return {};
  }
}

ResourcePackSelection UCore::GetCurrentWorldResourcePackSelection() const
{
  ResourcePackSelection selection;
  if (!WorldInstance || WorldInstance->GetWorldName().empty())
  {
    return selection;
  }
  selection.Primary = WorldInstance->GetResourcePacksPrimary();
  selection.Secondary = WorldInstance->GetResourcePacksSecondary();
  selection.WorldgenOwner = WorldInstance->GetWorldgenOwnerPackId();
  if (selection.Primary.empty())
  {
    selection.Primary = WorldInstance->GetResourcePacksEnabled();
  }
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  return selection;
}

bool UCore::ApplyResourcePacksToCurrentWorld(
    const ResourcePackSelection &selectionIn)
{
  if (!WorldInstance || WorldInstance->GetWorldName().empty())
  {
    return false;
  }
  ResourcePackSelection selection = selectionIn;
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  if (!ApplyResourcePacks(selection))
  {
    return false;
  }
  WorldInstance->SetResourcePackSelection(
      selection.Primary, selection.Secondary, selection.WorldgenOwner);
  SaveWorld(WorldInstance->GetWorldName());
  return true;
}

bool UCore::CreateWorldHeadless(const CreateWorldCliArgs &args,
                                CreateWorldReport &report)
{
  return WorldLifecycle.CreateWorldHeadless(*this, args, report);
}

} // namespace cutum
