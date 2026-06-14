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
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "App/Platform/IPlatformPaths.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "App/Core.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Engine/GeometryEngine.h"
#include "Storage/ObjectStorage.h"
#include "World/Prefabs/Prefab.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Creatures/Player/User.h"
#include "Version.h"
#include "Render/Engine/ViewEngine.h"
#include "World/Core/World.h"

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr int kMaxProjectRootSearchDepth = 8;

std::optional<std::filesystem::path>
TryFindProjectRoot(std::filesystem::path start)
{
  for (int depth = 0; depth < kMaxProjectRootSearchDepth; ++depth)
  {
    const auto textures_dir = start / "textures" / "blocks";
    const auto models_blocks_dir = start / "models" / "blocks";
    const auto prefabs_dir = start / "prefabs";
    const auto shaders_dir = start / "shaders";
    const bool hasTextures = std::filesystem::exists(textures_dir);
    const bool hasModels = std::filesystem::exists(models_blocks_dir);
    const bool hasPrefabs = std::filesystem::exists(prefabs_dir);
    const bool hasShaders =
        std::filesystem::exists(shaders_dir / "vshader_greedy.glsl");
    if (hasTextures && hasModels && hasPrefabs && hasShaders)
    {
      return start;
    }
    if (!start.has_parent_path())
    {
      break;
    }
    start = start.parent_path();
  }
  return std::nullopt;
}

std::filesystem::path FindProjectRoot(std::filesystem::path start)
{
  if (auto root = TryFindProjectRoot(start))
  {
    return *root;
  }
  return start;
}

bool ParseWorldNumberSuffix(const std::string &name, int &outNumber)
{
  constexpr const char *kPrefix = "World_";
  if (name.size() != 9)
  {
    return false;
  }
  if (name.compare(0, 6, kPrefix) != 0)
  {
    return false;
  }
  for (size_t i = 6; i < name.size(); ++i)
  {
    if (!std::isdigit(static_cast<unsigned char>(name[i])))
    {
      return false;
    }
  }
  outNumber = std::stoi(name.substr(6));
  return true;
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
             std::shared_ptr<UObjectStorage> object_storage_,
             std::shared_ptr<UPrefabLibrary> prefab_library_,
             std::shared_ptr<UWorld> World,
             std::shared_ptr<UGeometryEngine> geometries_,
             std::shared_ptr<UViewEngine> Views)
    : TextureBaseStorageInstance(texture_base_storage_),
      TextureCubeStorageInstance(texture_cube_storage_),
      ObjectStorageInstance(object_storage_),
      PrefabLibraryInstance(prefab_library_), WorldInstance(World),
      GeometryEngineInstance(geometries_), ViewEngineInstance(Views)
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
      ProceduralTemplate = ParseProceduralSettings(d);
      WorldSeed = ProceduralTemplate.seed;
      TerrainType = ProceduralGeneratorToString(ProceduralTemplate.generator);
      RenderDistanceChunks = d.value("render_distance_chunks", 4);
      StreamingEnabled = d.value("streaming_enabled", true);
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
        Render.greedyMeshing = r.value("greedy_meshing", true);
        Render.faceQuads = r.value("face_quads", true);
        Render.frustumCulling = r.value("frustum_culling", true);
        Render.batchCache = r.value("batch_cache", true);
        Render.creatureDebugBounds = r.value("creature_debug_bounds", false);
        Render.creatureTexturedParts = r.value("creature_textured_parts", true);
        Render.creatureWireframeOverlay =
            r.value("creature_wireframe_overlay", false);
        if (Render.greedyMeshing && !Render.faceQuads)
        {
          std::cout
              << "Render: greedy_meshing enabled — auto-enabling face_quads"
              << std::endl;
          Render.faceQuads = true;
        }
      }
      else
      {
        Render = RenderSettings::Default();
      }
      if (d.contains("ui") && d["ui"].is_object())
      {
        const json &u = d["ui"];
        Ui.legacyHud = u.value("legacy_hud", false);
        Ui.consoleKey = u.value("console_key", "grave");
        Ui.paletteKey = u.value("palette_key", "b");
        Ui.inventoryKey = u.value("inventory_key", "e");
        Ui.hotbarCount = std::clamp(u.value("hotbar_count", 1), 1, 2);
        std::string schemeStr = "classic";
        if (u.contains("control_scheme") && u["control_scheme"].is_string())
        {
          schemeStr = u["control_scheme"].get<std::string>();
        }
        else if (u.contains("block_input_profile") &&
                 u["block_input_profile"].is_string())
        {
          schemeStr = u["block_input_profile"].get<std::string>();
        }
        Ui.controlScheme = ControlSchemeFromString(schemeStr);
        Ui.placeClickMaxSeconds = u.value("place_click_max_seconds", 0.20f);
        Ui.breakHoldMinSeconds = u.value("break_hold_min_seconds", 0.50f);
        Ui.breakDurationSeconds = u.value("break_duration_seconds", 0.25f);
        Ui.rmbDragThresholdPx = u.value("rmb_drag_threshold_px", 4);
      }
    }
    else
    {
      DefaultWorldName.clear();
      DefaultUserName = "Username";
      WorldSeed = 12345u;
      TerrainType = "heightmap";
      ProceduralTemplate = ProceduralSettings{};
      ProceduralTemplate.seed = WorldSeed;
      RenderDistanceChunks = 4;
      StreamingEnabled = true;
      Render = RenderSettings::Default();
    }

    TextureBaseStorageFileName = WorkDir / "textures" / "blocks";
    TextureCubeStorageFileName = WorkDir / "models" / "blocks";
    ObjectStorageFileName = WorkDir / "models" / "objects";
    PrefabsPath = WorkDir / "prefabs";

    auto blockDefinitions = std::make_shared<UBlockDefinitionStorage>();
    blockDefinitions->Load(TextureCubeStorageFileName.string());
    TextureCubeStorageInstance->SetBlockDefinitions(blockDefinitions);
    WorldInstance->SetBlockDefinitionStorage(blockDefinitions);

    auto creatureDefinitions = std::make_shared<UCreatureDefinitionStorage>();
    creatureDefinitions->Load((WorkDir / "models" / "creatures").string());
    WorldInstance->SetCreatureDefinitionStorage(creatureDefinitions);

    auto skinDefinitions = std::make_shared<USkinDefinitionStorage>();
    skinDefinitions->Load((WorkDir / "models" / "skins").string());
    WorldInstance->SetSkinDefinitionStorage(skinDefinitions);

    CreatureTextureStorageInstance =
        std::make_shared<UCreatureTextureStorage>();
    CreatureTextureStorageInstance->LoadFromCreatureAndSkinRoots(
        (WorkDir / "models" / "creatures").string(),
        (WorkDir / "models" / "skins").string());

    TextureBaseStorageInstance->Load(TextureBaseStorageFileName.string());

    TextureCubeStorageInstance->Load(TextureCubeStorageFileName.string());

    ObjectStorageInstance->Load(ObjectStorageFileName.string());

    WorldInstance->RefreshBlockRegistry();

    if (PrefabLibraryInstance)
    {
      PrefabLibraryInstance->Load(PrefabsPath.string(),
                                  WorldInstance->GetBlockRegistry());
      WorldInstance->SetPrefabLibrary(PrefabLibraryInstance.get());
      if (auto user = WorldInstance->GetCurrentUser())
      {
        WorldInstance->EnsurePlayerHotbarCount(
            user, static_cast<size_t>(Ui.hotbarCount));
      }
    }

    WorldInstance->SetProceduralSettings(ProceduralTemplate);
    std::cout << "Procedural: "
              << ProceduralGeneratorToString(ProceduralTemplate.generator)
              << " (" << VerticalModeToString(ProceduralTemplate.vertical)
              << ", seed=" << ProceduralTemplate.seed
              << ", sea=" << ProceduralTemplate.seaLevel
              << ", maxY=" << ProceduralTemplate.maxHeight
              << ", caves=" << (ProceduralTemplate.enableCaves ? "1" : "0")
              << ", trees=" << (ProceduralTemplate.enableTrees ? "1" : "0")
              << ")" << std::endl;
    WorldInstance->SetStreamingEnabled(StreamingEnabled);
    WorldInstance->SetRenderDistanceChunks(RenderDistanceChunks);
    WorldInstance->SetStepUpEnabled(StepUpEnabled);
    WorldInstance->SetEntityCollisionEnabled(EntityCollisionEnabled);
    WorldInstance->SetRenderSettings(Render);
    if (GeometryEngineInstance)
    {
      GeometryEngineInstance->SetRenderSettings(Render);
      GeometryEngineInstance->SetCreatureTextureStorage(
          CreatureTextureStorageInstance);
    }
    std::cout << "Render: greedy=" << Render.greedyMeshing
              << " face_quads=" << Render.faceQuads
              << " frustum=" << Render.frustumCulling
              << " batch_cache=" << Render.batchCache << std::endl;
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
  WriteProceduralSettings(system_data, ProceduralTemplate);
  system_data["render_distance_chunks"] = RenderDistanceChunks;
  system_data["streaming_enabled"] = StreamingEnabled;
  json gameplay;
  gameplay["step_up"] = StepUpEnabled;
  gameplay["entity_collision"] = EntityCollisionEnabled;
  system_data["gameplay"] = gameplay;
  json render;
  render["greedy_meshing"] = Render.greedyMeshing;
  render["face_quads"] = Render.faceQuads;
  render["frustum_culling"] = Render.frustumCulling;
  render["batch_cache"] = Render.batchCache;
  render["creature_debug_bounds"] = Render.creatureDebugBounds;
  render["creature_textured_parts"] = Render.creatureTexturedParts;
  render["creature_wireframe_overlay"] = Render.creatureWireframeOverlay;
  system_data["render"] = render;
  WriteUiSettings(system_data, Ui);

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
  WorldSeed = ProceduralTemplate.seed;

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

  WorldInstance->SetStreamingEnabled(StreamingEnabled);
  WorldInstance->SetRenderDistanceChunks(RenderDistanceChunks);
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
  ProceduralTemplate = settings;
  WorldSeed = settings.seed;
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.generator);
  ResolveProceduralDefaults(ProceduralTemplate);
  ApplyGeneratorTierDefaults(ProceduralTemplate);
}

void UCore::CreateNewWorldFromTemplate()
{
  WorldSeed += 1;
  ProceduralTemplate.seed = WorldSeed;
  ResolveProceduralDefaults(ProceduralTemplate);
  ApplyGeneratorTierDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.generator);
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
    ProceduralTemplate.generator = ProceduralGeneratorFromString(terrain_type);
  }
  ProceduralTemplate.seed = WorldSeed;
  ResolveProceduralDefaults(ProceduralTemplate);
  ApplyGeneratorTierDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.generator);
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
        ProceduralTemplate = ParseProceduralSettings(d);
        WorldSeed = ProceduralTemplate.seed;
      }
      catch (const json::exception &e)
      {
        std::cerr << "CreateWorldFromProceduralConfig: config parse error: "
                  << e.what() << std::endl;
      }
    }
  }

  WorldSeed += 1;
  ProceduralTemplate.seed = WorldSeed;
  ResolveProceduralDefaults(ProceduralTemplate);
  ApplyGeneratorTierDefaults(ProceduralTemplate);
  TerrainType = ProceduralGeneratorToString(ProceduralTemplate.generator);

  std::cout << "Core::CreateWorldFromProceduralConfig: " << TerrainType << " ("
            << VerticalModeToString(ProceduralTemplate.vertical)
            << ", seed=" << ProceduralTemplate.seed << ")" << std::endl;

  CreateNewWorldWithCurrentSettings();
}

void UCore::CreateNewWorldWithCurrentSettings()
{
  const std::string new_world_name = AllocateNextWorldName();
  DefaultWorldName = new_world_name;
  ActiveWorldFolder = WorldFolderPath(new_world_name);
  std::filesystem::create_directories(ActiveWorldFolder / "chunks");

  std::cout << "Core::CreateWorld: new world '" << new_world_name << "' at "
            << ActiveWorldFolder.string() << std::endl;

  WorldInstance->SetProceduralSettings(ProceduralTemplate);
  WorldInstance->Create(new_world_name);
  if (WorldInstance->GetCurrentUser() == nullptr)
  {
    WorldInstance->GenerateUsers();
  }
  SaveWorld(new_world_name);
  LoadWorldList(WorldPath.string());
}

void UCore::LoadWorld(const std::string &world_name)
{
  ActiveWorldFolder = WorldFolderPath(world_name);
  WorldInstance->Load(ActiveWorldFolder.string());
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
      const std::string name = entry.path().filename().string();
      if (std::find(WorldList.begin(), WorldList.end(), name) ==
          WorldList.end())
      {
        WorldList.push_back(name);
      }
    }
  }
  catch (const std::filesystem::filesystem_error &ex)
  {
    std::cerr << ex.what() << std::endl;
  }
}

} // namespace cutum
