// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "App/Core.h"
#include "App/LegacyConfigAdapter.h"
#include "App/Platform/GameDataRoot.h"
#include "App/Platform/IPlatformPaths.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Core/ColorUtil.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/Skeletal/CreatureSkeletalGeoCache.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/CreaturePackMerge.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "Version.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/ProceduralConfigIO.h"

using json = nlohmann::json;

namespace cutum
{

namespace
{

bool ParseWorldNumberSuffix(const std::string &Name, int &outNumber)
{
  constexpr const char *kPrefix = "World_";
  if (Name.size() != 9)
  {
    return false;
  }
  if (Name.compare(0, 6, kPrefix) != 0)
  {
    return false;
  }
  for (size_t i = 6; i < Name.size(); ++i)
  {
    if (!std::isdigit(static_cast<unsigned char>(Name[i])))
    {
      return false;
    }
  }
  outNumber = std::stoi(Name.substr(6));
  return true;
}

bool ResourcePackSelectionEqual(const ResourcePackSelection &a,
                                const ResourcePackSelection &b)
{
  return a.Primary == b.Primary && a.Secondary == b.Secondary &&
         a.WorldgenOwner == b.WorldgenOwner;
}

} // namespace

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

std::filesystem::path
UCore::WorldFolderPath(const std::string &world_name) const
{
  return WorldPath / world_name;
}

std::string UCore::AllocateNextWorldName() const
{
  int maxNumber = 0;
  if (std::filesystem::exists(WorldPath) &&
      std::filesystem::is_directory(WorldPath))
  {
    for (const auto &entry : std::filesystem::directory_iterator(WorldPath))
    {
      if (!entry.is_directory())
      {
        continue;
      }
      int number = 0;
      if (ParseWorldNumberSuffix(entry.path().filename().string(), number))
      {
        maxNumber = std::max(maxNumber, number);
      }
    }
  }

  char nameBuffer[16];
  std::snprintf(nameBuffer, sizeof(nameBuffer), "World_%03d", maxNumber + 1);
  return nameBuffer;
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
  if (auto *paths = IPlatformPaths::TryGet())
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
      TerrainType = d.value("terrain", "heightmap");
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
        Render.GreedyMeshing = r.value("greedy_meshing", true);
        Render.AsyncMeshing = r.value("async_meshing", true);
        Render.FaceQuads = r.value("face_quads", true);
        Render.FrustumCulling = r.value("frustum_culling", true);
        Render.BatchCache = r.value("batch_cache", true);
        Render.CreatureDebugBounds = r.value("creature_debug_bounds", false);
        Render.CreatureTexturedParts = r.value("creature_textured_parts", true);
        Render.CreatureWireframeOverlay =
            r.value("creature_wireframe_overlay", false);
        Render.DistanceFog = r.value("distance_fog", true);
        Render.DistanceFogStartRatio =
            r.value("distance_fog_start_ratio", 0.55f);
        Render.DistanceFogDensity = r.value("distance_fog_density", 0.85f);
        Render.DistanceFogHorizontal = r.value("distance_fog_horizontal", true);
        Render.AltitudeAdaptiveFog = r.value("altitude_adaptive_fog", true);
        Render.AltitudeFogThresholdBlocks =
            r.value("altitude_fog_threshold_blocks", 32);
        Render.AltitudeFogPenaltyPer16Blocks =
            r.value("altitude_fog_penalty_per_16_blocks", 0.05f);
        Render.GradientSky = r.value("gradient_sky", true);
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

    const glm::vec3 bg = ParseHexColor(ResourcePacks.PlaceholderBackground,
                                       glm::vec3(0.42f, 0.29f, 0.62f));
    PlaceholderCacheInstance = std::make_shared<UPlaceholderTextureCache>(
        ExeDir / ".placeholder_cache", ResourcePacks.PlaceholderTileSize, bg,
        static_cast<size_t>(ResourcePacks.PlaceholderCacheMaxEntries));

    WorldInstance->SetBlockMergeRegistry(BlockMergeRegistryInstance);
    WorldInstance->SetOnAfterWorldDataLoaded(
        [this]() { ApplyResourcePacksAfterWorldDataLoaded(); });

    WorldInstance->SetBlockDefinitionStorage(blockDefinitions);

    auto creatureDefinitions = std::make_shared<UCreatureDefinitionStorage>();
    creatureDefinitions->Load((WorkDir / "models" / "creatures").string());
    CreatureSkeletalGeoCache::Instance().SetCreaturesRoot(
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

void UCore::RebuildBlockTexturesFromMergeRegistry()
{
  if (!BlockMergeRegistryInstance || !BlockDefinitionsInstance ||
      !TextureBaseStorageInstance || !TextureCubeStorageInstance)
  {
    return;
  }
  if (WorldInstance)
  {
    WorldInstance->WaitForPendingMeshJobs();
  }
  BlockMergeRegistryInstance->PopulateBlockDefinitionStorage(
      *BlockDefinitionsInstance);
  TextureBaseStorageInstance->Clear();
  BlockMergeRegistryInstance->PopulateTextureBaseStorage(
      *TextureBaseStorageInstance);
  TextureCubeStorageInstance->Clear();
  TextureCubeStorageInstance->SetBlockDefinitions(BlockDefinitionsInstance);
  TextureCubeStorageInstance->BuildFromDescriptors(
      BlockMergeRegistryInstance->GetCubeDescriptors());
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
  if (!BlockMergeRegistryInstance || !WorldInstance)
  {
    return false;
  }
  const BlockId id =
      BlockMergeRegistryInstance->RegisterRuntimeBlock(def, textureStems);
  RuntimeBlockFlushPending = true;
  FlushRuntimeBlockOverlay();
  if (id == BLOCK_AIR &&
      BlockMergeRegistryInstance->GetNameToId().count(def.Name) == 0)
  {
    return false;
  }
  const BlockId resolved = BlockMergeRegistryInstance->ResolveName(def.Name);
  if (resolved == BLOCK_AIR)
  {
    return false;
  }
  return true;
}

void UCore::BeginRuntimeBlockBatch() { ++RuntimeBlockBatchDepth; }

void UCore::EndRuntimeBlockBatch()
{
  RuntimeBlockBatchDepth = std::max(0, RuntimeBlockBatchDepth - 1);
  FlushRuntimeBlockOverlay();
}

void UCore::FlushRuntimeBlockOverlay()
{
  if (!RuntimeBlockFlushPending || RuntimeBlockBatchDepth > 0 ||
      !BlockMergeRegistryInstance)
  {
    return;
  }
  BlockMergeRegistryInstance->FlushRuntimeOverlay();
  RuntimeBlockFlushPending = false;
  RebuildBlockTexturesFromMergeRegistry();
  if (WorldInstance)
  {
    WorldInstance->OnBlockRegistryRuntimeOverlayChanged();
  }
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
  CreateNewWorldWithCurrentSettings();
}

void UCore::RefreshWorldList()
{
  std::filesystem::create_directories(WorldPath);
  LoadWorldList(WorldPath.string());
}

void UCore::LoadWorldByName(const std::string &world_name)
{
  DefaultWorldName = world_name;
  LoadWorld(world_name);
}

void UCore::CreateWorld(const std::string &terrain_type)
{
  WorldSeed += 1;
  if (!terrain_type.empty())
  {
    TerrainType = terrain_type;
    ProceduralTemplate.Generator = ProceduralGeneratorFromString(terrain_type);
  }
  ProceduralTemplate.Seed = WorldSeed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);
  PendingNewWorldSettings.reset();
  CreateNewWorldWithCurrentSettings();
}

void UCore::CreateWorldFromProceduralConfig()
{
  if (!ConfigFilePath.empty() && std::filesystem::exists(ConfigFilePath))
  {
    std::ifstream file(ConfigFilePath.string());
    if (file.is_open())
    {
      std::stringstream buffer;
      buffer << file.rdbuf();
      try
      {
        const json d = json::parse(buffer.str());
        ProceduralTemplate = ParseProceduralTemplateFromConfig(d);
        WorldSeed = ProceduralTemplate.Seed;
      }
      catch (const json::exception &e)
      {
        std::cerr << "CreateWorldFromProceduralConfig: config parse error: "
                  << e.what() << std::endl;
      }
    }
  }

  WorldSeed += 1;
  ProceduralTemplate.Seed = WorldSeed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);

  std::cout << "Core::CreateWorldFromProceduralConfig: " << TerrainType
            << " (Seed=" << ProceduralTemplate.Seed << ")" << std::endl;

  PendingNewWorldSettings.reset();
  CreateNewWorldWithCurrentSettings();
}

void UCore::CreateNewWorldWithCurrentSettings()
{
  const std::string new_world_name = SetupNewWorldForCreation();
  WorldInstance->Create(new_world_name);
  WorldInstance->GenerateUsers();
  SaveWorld(new_world_name);
  LoadWorldList(WorldPath.string());
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
  std::filesystem::create_directories(WorldPath);
  LoadWorldList(WorldPath.string());
}

bool UCore::NeedsCreateWorldOnStartup() const
{
  return ShouldCreateWorldOnStartup();
}

void UCore::PrepareLoadWorld(const std::string &world_name)
{
  DefaultWorldName = world_name;
  ActiveWorldFolder = WorldFolderPath(world_name);
}

void UCore::FinalizeLoadedWorld()
{
  ApplyRuntimeStreamingToWorld();
  if (!DefaultUserName.empty())
  {
    if (!WorldInstance->SetCurrentUserName(DefaultUserName))
    {
      std::cerr << "Core::FinalizeLoadedWorld: user '" << DefaultUserName
                << "' not found." << std::endl;
    }
  }
  if (WorldInstance->GetCurrentUser() == nullptr)
  {
    WorldInstance->GenerateUsers();
  }
}

void UCore::FinalizeEnterGameSession()
{
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
}

void UCore::RefreshWorldListAfterSave() { LoadWorldList(WorldPath.string()); }

std::string UCore::SetupNewWorldForCreation()
{
  ResourcePackSelection selection = PendingNewWorldPackSelection;
  const std::vector<std::string> legacyPacks = PendingNewWorldResourcePacks;
  PendingNewWorldPackSelection = {};
  PendingNewWorldResourcePacks.clear();
  if (selection.Primary.empty())
  {
    selection.Primary = NormalizeEnabledPackIds(legacyPacks);
  }
  if (selection.Primary.empty())
  {
    selection = GetDefaultResourcePackSelection();
  }
  selection.Primary = NormalizeEnabledPackIds(selection.Primary);
  selection.Secondary = NormalizeEnabledPackIds(selection.Secondary);
  if (selection.Primary.empty())
  {
    selection = GetDefaultResourcePackSelection();
    selection.Primary = NormalizeEnabledPackIds(selection.Primary);
    selection.Secondary = NormalizeEnabledPackIds(selection.Secondary);
  }
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }

  WorldInstance->SetResourcePackSelection(
      selection.Primary, selection.Secondary, selection.WorldgenOwner);
  if (!ResourcePackSelectionEqual(selection, ActivePackSelection))
  {
    ApplyResourcePacks(selection);
  }

  const std::string new_world_name = AllocateNextWorldName();
  DefaultWorldName = new_world_name;
  ActiveWorldFolder = WorldFolderPath(new_world_name);
  std::filesystem::create_directories(ActiveWorldFolder / "chunks");

  std::cout << "Core::CreateWorld: new world '" << new_world_name << "' at "
            << ActiveWorldFolder.string() << std::endl;

  ProceduralSettings worldSettings = ProceduralTemplate;
  if (PendingNewWorldSettings.has_value())
  {
    worldSettings = *PendingNewWorldSettings;
    PendingNewWorldSettings.reset();
  }
  else
  {
    worldSettings.Seed = WorldSeed;
    ResetToGeneratorDefaults(worldSettings);
  }
  ResolveProceduralDefaults(worldSettings);
  ApplyGeneratorTierDefaults(worldSettings);
  worldSettings.AsyncChunkGeneration = ProceduralTemplate.AsyncChunkGeneration;
  worldSettings.AsyncChunkIo = ProceduralTemplate.AsyncChunkIo;
  worldSettings.MaxChunkCommitsPerFrame =
      ProceduralTemplate.MaxChunkCommitsPerFrame;

  WorldInstance->SetProceduralSettings(worldSettings);
  WorldInstance->SetRenderSettings(Render);
  return new_world_name;
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
  ActiveWorldFolder = WorldFolderPath(world_name);
  WorldInstance->Load(ActiveWorldFolder.string());
  ApplyRuntimeStreamingToWorld();
  if (!DefaultUserName.empty())
  {
    if (!WorldInstance->SetCurrentUserName(DefaultUserName))
    {
      std::cerr << "Core::LoadWorld: user '" << DefaultUserName
                << "' not found." << std::endl;
    }
  }
  if (WorldInstance->GetCurrentUser() == nullptr)
  {
    WorldInstance->GenerateUsers();
  }
}

void UCore::LoadLastWorld()
{
  if (DefaultWorldName.empty())
  {
    std::cerr << "Core::LoadLastWorld: default_world is not set in config."
              << std::endl;
    return;
  }

  std::cout << "Loading last World: " << DefaultWorldName
            << " (user: " << DefaultUserName << ")" << std::endl;

  LoadWorld(DefaultWorldName);

  if (!DefaultUserName.empty())
  {
    if (!WorldInstance->SetCurrentUserName(DefaultUserName))
    {
      std::cerr << "Core::LoadLastWorld: user '" << DefaultUserName
                << "' not found, using current user." << std::endl;
    }
  }

  WorldInstance->FinalizePlayerAfterWorldLoad();
}

void UCore::SaveWorld(const std::string &world_name)
{
  if (ActiveWorldFolder.empty())
  {
    ActiveWorldFolder = WorldFolderPath(world_name);
  }
  if (WorldInstance && BlockMergeRegistryInstance)
  {
    WorldInstance->SetCatalogFingerprint(
        BlockMergeRegistryInstance->ComputeCatalogFingerprint());
  }
  WorldInstance->Save(ActiveWorldFolder.string());
}

void UCore::LoadWorldList(const std::string &world_path)
{
  WorldList.clear();

  if (!std::filesystem::exists(world_path) ||
      !std::filesystem::is_directory(world_path))
  {
    return;
  }

  try
  {
    for (const auto &entry : std::filesystem::directory_iterator(world_path))
    {
      if (!entry.is_directory())
      {
        continue;
      }
      const std::string Name = entry.path().filename().string();
      if (std::find(WorldList.begin(), WorldList.end(), Name) ==
          WorldList.end())
      {
        WorldList.push_back(Name);
      }
    }
  }
  catch (const std::filesystem::filesystem_error &ex)
  {
    std::cerr << ex.what() << std::endl;
  }
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

std::vector<std::string>
UCore::NormalizeEnabledPackIds(const std::vector<std::string> &requested) const
{
  const auto installed = ListInstalledResourcePacks();
  std::unordered_set<std::string> installedIds;
  for (const auto &pack : installed)
  {
    installedIds.insert(pack.Id);
  }
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  for (const auto &id : requested)
  {
    if (seen.count(id) != 0)
    {
      continue;
    }
    if (installedIds.count(id) != 0)
    {
      seen.insert(id);
      result.push_back(id);
    }
    else
    {
      std::cerr << "UCore: resource pack not installed, skipping: " << id
                << std::endl;
    }
  }
  return result;
}

bool UCore::ApplyResourcePacks(const std::vector<std::string> &enabledIds)
{
  ResourcePackSelection selection;
  selection.Primary = enabledIds;
  if (selection.WorldgenOwner.empty() && !selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  return ApplyResourcePacks(selection);
}

bool UCore::ApplyResourcePacks(const ResourcePackSelection &selectionIn)
{
  if (!BlockMergeRegistryInstance)
  {
    return false;
  }
  ResourcePackSelection selection = selectionIn;
  selection.Primary = NormalizeEnabledPackIds(selection.Primary);
  selection.Secondary = NormalizeEnabledPackIds(selection.Secondary);
  if (selection.Primary.empty())
  {
    selection = GetDefaultResourcePackSelection();
    selection.Primary = NormalizeEnabledPackIds(selection.Primary);
    selection.Secondary = NormalizeEnabledPackIds(selection.Secondary);
  }
  if (selection.Primary.empty())
  {
    std::cerr << "UCore::ApplyResourcePacks: no packs available" << std::endl;
    return false;
  }
  if (selection.WorldgenOwner.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }

  UResourcePackResolver resolver;
  const auto packs = resolver.Resolve(selection, WorkDir, ExeDir);
  if (packs.empty())
  {
    std::cerr << "UCore::ApplyResourcePacks: no packs resolved" << std::endl;
    return false;
  }

  if (!PlaceholderCacheInstance)
  {
    const glm::vec3 bg = ParseHexColor(ResourcePacks.PlaceholderBackground,
                                       glm::vec3(0.42f, 0.29f, 0.62f));
    PlaceholderCacheInstance = std::make_shared<UPlaceholderTextureCache>(
        ExeDir / ".placeholder_cache", ResourcePacks.PlaceholderTileSize, bg,
        static_cast<size_t>(ResourcePacks.PlaceholderCacheMaxEntries));
  }

  BlockMergeRegistryInstance->SetPrimaryPackIds(selection.Primary);
  BlockMergeRegistryInstance->SetWorldgenOwnerPackId(selection.WorldgenOwner);
  BlockMergeRegistryInstance->Rebuild(packs, PlaceholderCacheInstance,
                                      ResourcePacks.PlaceholderTileSize);
  ActivePackSelection = selection;
  ActiveResourcePacksEnabled = selection.AllIds();

  if (ObjectLibraryInstance && WorldInstance)
  {
    ObjectLibraryInstance->LoadMerged(ObjectsPath, packs,
                                      WorldInstance->GetBlockRegistry());
    WorldInstance->SetObjectLibrary(ObjectLibraryInstance.get());
  }

  RebuildBlockTexturesFromMergeRegistry();

  std::cout << "Resource packs: applied " << packs.size() << " pack(s)";
  for (const auto &p : packs)
  {
    std::cout << " [" << p.Id << "]";
  }
  std::cout << " (" << BlockMergeRegistryInstance->GetCubeDescriptors().size()
            << " block types)" << std::endl;

  if (WorldInstance)
  {
    WorldInstance->OnBlockRegistryChanged();
    RebuildBlockTexturesFromMergeRegistry();
  }
  ReloadCreatureCatalog(packs);
  if (WorldInstance && BlockMergeRegistryInstance)
  {
    WorldInstance->SetCatalogFingerprint(
        BlockMergeRegistryInstance->ComputeCatalogFingerprint());
  }
  return true;
}

void UCore::ReloadCreatureCatalog(
    const std::vector<ResourcePackManifest> &packs)
{
  if (!WorldInstance || !CreatureTextureStorageInstance)
  {
    return;
  }
  auto defs = WorldInstance->GetCreatureDefinitionStorage();
  if (!defs)
  {
    return;
  }
  ApplyCreaturePackOverlays(*defs, *CreatureTextureStorageInstance,
                            WorldInstance->GetSkinDefinitionStorage().get(),
                            WorkDir / "models" / "creatures",
                            WorkDir / "models" / "skins", packs);
  WorldInstance->OnCreatureCatalogChanged();
}

void UCore::ApplyResourcePacksAfterWorldDataLoaded()
{
  ResourcePackSelection selection;
  selection.Primary = WorldInstance->GetResourcePacksPrimary();
  selection.Secondary = WorldInstance->GetResourcePacksSecondary();
  selection.WorldgenOwner = WorldInstance->GetWorldgenOwnerPackId();
  if (selection.Primary.empty())
  {
    selection.Primary = WorldInstance->GetResourcePacksEnabled();
  }
  if (selection.Primary.empty())
  {
    selection = GetDefaultResourcePackSelection();
    WorldInstance->SetResourcePackSelection(
        selection.Primary, selection.Secondary, selection.WorldgenOwner);
  }
  const std::string storedFingerprint = WorldInstance->GetCatalogFingerprint();
  ApplyResourcePacks(selection);
  if (BlockMergeRegistryInstance && !storedFingerprint.empty())
  {
    const std::string currentFingerprint =
        BlockMergeRegistryInstance->ComputeCatalogFingerprint();
    if (storedFingerprint != currentFingerprint)
    {
      std::cerr << "WARNING: block catalog fingerprint mismatch for world '"
                << WorldInstance->GetWorldName()
                << "'. Blocks/textures may not match the saved terrain."
                << std::endl;
    }
  }
}

void UCore::CreateNewWorldWithSettings(
    const ProceduralSettings &settings,
    const std::vector<std::string> &resourcePacks)
{
  ResourcePackSelection selection;
  selection.Primary = resourcePacks;
  if (!selection.Primary.empty())
  {
    selection.WorldgenOwner = selection.Primary.front();
  }
  CreateNewWorldWithSettings(settings, selection);
}

void UCore::CreateNewWorldWithSettings(
    const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  ApplyNewWorldCreationRequest(settings, resourcePacks);
  CreateNewWorldWithCurrentSettings();
}

void UCore::ApplyNewWorldCreationRequest(
    const ProceduralSettings &settings,
    const ResourcePackSelection &resourcePacks)
{
  PendingNewWorldSettings = settings;
  PendingNewWorldSettings->Seed = settings.Seed;
  WorldSeed = settings.Seed + 1;
  PendingNewWorldSettings->Seed = WorldSeed;

  ProceduralTemplate.Generator = settings.Generator;
  ProceduralTemplate.Seed = WorldSeed;
  ResetToGeneratorDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.Generator);

  PendingNewWorldPackSelection = resourcePacks;
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
    if (!d.contains("resource_packs") || !d["resource_packs"].is_object())
    {
      return {};
    }
    const auto &rp = d["resource_packs"];
    if (!rp.contains("enabled") || !rp["enabled"].is_array())
    {
      return {};
    }
    std::vector<std::string> result;
    for (const auto &id : rp["enabled"])
    {
      if (id.is_string())
      {
        result.push_back(id.get<std::string>());
      }
    }
    return result;
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
  report = CreateWorldReport{};
  report.WorldName = args.WorldName;
  report.Seed = args.Seed;
  report.Generator = ProceduralGeneratorToString(args.Generator);
  report.Preset = args.Preset;
  report.RadiusChunks = args.RadiusChunks;

  if (!WorldInstance)
  {
    report.Error = "World instance is not initialized.";
    return false;
  }

  ResourcePackSelection selection = GetDefaultResourcePackSelection();
  if (!args.PrimaryPacks.empty())
  {
    selection.Primary = NormalizeEnabledPackIds(args.PrimaryPacks);
    selection.WorldgenOwner = args.WorldgenOwnerPack.empty()
                                  ? selection.Primary.front()
                                  : args.WorldgenOwnerPack;
  }
  selection.Primary = NormalizeEnabledPackIds(selection.Primary);
  selection.Secondary = NormalizeEnabledPackIds(selection.Secondary);
  if (!ApplyResourcePacks(selection))
  {
    report.Error = "Failed to apply resource packs.";
    return false;
  }

  ProceduralSettings settings = ProceduralTemplate;
  settings.Generator = args.Generator;
  settings.Seed = args.Seed;
  ResetToGeneratorDefaults(settings);
  settings.Generator = args.Generator;
  settings.Seed = args.Seed;
  ApplyWorldGenPreset(settings, args.Preset);
  settings.AsyncChunkGeneration = false;
  settings.AsyncChunkIo = false;
  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);

  RenderDistanceChunks = std::max(1, args.RadiusChunks);
  WorldInstance->SetRenderDistanceChunks(RenderDistanceChunks);
  WorldInstance->SetStreamingEnabled(false);

  const std::filesystem::path output_root = args.OutputRoot.is_absolute()
                                                ? args.OutputRoot
                                                : (ExeDir / args.OutputRoot);
  std::error_code ec;
  std::filesystem::create_directories(output_root, ec);
  ActiveWorldFolder = output_root / args.WorldName;
  std::filesystem::create_directories(ActiveWorldFolder, ec);
  std::filesystem::create_directories(ActiveWorldFolder / "chunks", ec);
  DefaultWorldName = args.WorldName;
  WorldPath = output_root;

  WorldInstance->SetResourcePackSelection(
      selection.Primary, selection.Secondary, selection.WorldgenOwner);
  WorldInstance->SetProceduralSettings(settings);
  WorldInstance->SetRenderSettings(Render);

  try
  {
    WorldInstance->Create(args.WorldName);
    WorldInstance->GenerateUsers();
    if (BlockMergeRegistryInstance)
    {
      WorldInstance->SetCatalogFingerprint(
          BlockMergeRegistryInstance->ComputeCatalogFingerprint());
    }
    WorldInstance->Save(ActiveWorldFolder.string());
  }
  catch (const std::exception &e)
  {
    report.Error = e.what();
    return false;
  }

  const std::filesystem::path chunks_dir = ActiveWorldFolder / "chunks";
  int chunk_files = 0;
  if (std::filesystem::exists(chunks_dir))
  {
    for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
    {
      if (entry.path().extension() == ".cchunk")
      {
        ++chunk_files;
      }
    }
  }

  report.Success = true;
  report.WorldPath = ActiveWorldFolder.string();
  report.ChunkFiles = chunk_files;
  report.SpawnY = WorldInstance->GetSpawnPoint().y;
  return true;
}

} // namespace cutum
