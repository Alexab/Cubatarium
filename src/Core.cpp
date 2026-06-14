//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <system_error>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "Core.h"
#include "Version.h"
#include "BlockDefinitionStorage.h"
#include "ProceduralConfigIO.h"
#include "ProceduralSettings.h"
#include "World.h"
#include "Creature.h"
#include "CreatureDefinitionStorage.h"
#include "CreatureTextureStorage.h"
#include "SkinDefinitionStorage.h"
#include "ProceduralConfigIO.h"
#include "ProceduralSettings.h"
#include "TextureCube.h"
#include "TextureBase.h"
#include "ObjectStorage.h"
#include "Prefab.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "User.h"

using json = nlohmann::json;

namespace cutum {

namespace {

constexpr int kMaxProjectRootSearchDepth = 8;

std::optional<std::filesystem::path> TryFindProjectRoot(std::filesystem::path start)
{
 for (int depth = 0; depth < kMaxProjectRootSearchDepth; ++depth) {
  const auto textures_dir = start / "textures" / "blocks";
  const auto models_blocks_dir = start / "models" / "blocks";
  const auto prefabs_dir = start / "prefabs";
  const auto shaders_dir = start / "shaders";
  const bool hasTextures = std::filesystem::exists(textures_dir);
  const bool hasModels = std::filesystem::exists(models_blocks_dir);
  const bool hasPrefabs = std::filesystem::exists(prefabs_dir);
  const bool hasShaders = std::filesystem::exists(shaders_dir / "vshader_greedy.glsl");
  if (hasTextures && hasModels && hasPrefabs && hasShaders) {
   return start;
  }
  if (!start.has_parent_path()) {
   break;
  }
  start = start.parent_path();
 }
 return std::nullopt;
}

std::filesystem::path FindProjectRoot(std::filesystem::path start)
{
 if (auto root = TryFindProjectRoot(start)) {
  return *root;
 }
 return start;
}

bool ParseWorldNumberSuffix(const std::string& name, int& outNumber)
{
 constexpr const char* kPrefix = "World_";
 if (name.size() != 9) {
  return false;
 }
 if (name.compare(0, 6, kPrefix) != 0) {
  return false;
 }
 for (size_t i = 6; i < name.size(); ++i) {
  if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
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
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

UCore::UCore(std::shared_ptr<TextureBaseStorage> texture_base_storage_,
           std::shared_ptr<TextureCubeStorage> texture_cube_storage_,
           std::shared_ptr<ObjectStorage> object_storage_,
           std::shared_ptr<PrefabLibrary> prefab_library_,
           std::shared_ptr<UWorld> world_,
           std::shared_ptr<UGeometryEngine> geometries_,
           std::shared_ptr<UViewEngine> views_)
 : TextureBaseStorageInstance(texture_base_storage_)
 , TextureCubeStorageInstance(texture_cube_storage_)
 , ObjectStorageInstance(object_storage_)
 , PrefabLibraryInstance(prefab_library_)
 , WorldInstance(world_)
 , GeometryEngineInstance(geometries_)
 , ViewEngineInstance(views_)
{
}

std::filesystem::path UCore::WorldFolderPath(const std::string& world_name) const
{
 return WorldPath / world_name;
}

std::string UCore::AllocateNextWorldName() const
{
 int maxNumber = 0;
 if (std::filesystem::exists(WorldPath) && std::filesystem::is_directory(WorldPath)) {
  for (const auto& entry : std::filesystem::directory_iterator(WorldPath)) {
   if (!entry.is_directory()) {
    continue;
   }
   int number = 0;
   if (ParseWorldNumberSuffix(entry.path().filename().string(), number)) {
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
 if (default_world_name.empty()) {
  return true;
 }
 if (!std::filesystem::exists(WorldPath) || !std::filesystem::is_directory(WorldPath)) {
  return true;
 }

 bool hasWorldFolder = false;
 for (const auto& entry : std::filesystem::directory_iterator(WorldPath)) {
  if (entry.is_directory()) {
   hasWorldFolder = true;
   break;
  }
 }
 if (!hasWorldFolder) {
  return true;
 }

 const auto worldFolder = WorldFolderPath(default_world_name);
 return !std::filesystem::exists(worldFolder);
}

void UCore::LoadConfig(const std::string& config_file_name)
{
 exeDir_ = GetExecutableDirectory();
 const auto cwd = std::filesystem::current_path();
 std::filesystem::path project_dir = cwd;
 if (auto fromExe = TryFindProjectRoot(exeDir_)) {
  project_dir = *fromExe;
 } else if (auto fromCwd = TryFindProjectRoot(cwd)) {
  project_dir = *fromCwd;
 } else {
  project_dir = FindProjectRoot(cwd);
 }
 std::error_code pathEc;
 std::filesystem::current_path(project_dir, pathEc);

 configFilePath_ = exeDir_ / config_file_name;
 WorldPath = exeDir_ / "worlds";
 WorkDir = project_dir;

 std::string val;
 bool configRead = false;
 std::ifstream file(configFilePath_.string());
 if (file.is_open()) {
  std::stringstream buffer;
  buffer << file.rdbuf();
  val = buffer.str();
  file.close();
  configRead = true;
 } else {
  std::cout << "Config not found, will create: " << configFilePath_.string() << std::endl;
 }

 try {
     if (configRead) {
      json d = json::parse(val);
      default_world_name = d.value("default_world", "");
      default_user_name = d.value("default_user", "");
      worldSeed_ = d.value("world_seed", 12345u);
      terrainType_ = d.value("terrain", "heightmap");
      proceduralSettings_ = ParseProceduralSettings(d);
      worldSeed_ = proceduralSettings_.seed;
      terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);
      renderDistanceChunks_ = d.value("render_distance_chunks", 4);
      streamingEnabled_ = d.value("streaming_enabled", true);
      if (d.contains("gameplay") && d["gameplay"].is_object()) {
       const json& gameplay = d["gameplay"];
       stepUpEnabled_ = gameplay.value("step_up", true);
       entityCollisionEnabled_ = gameplay.value("entity_collision", true);
      } else {
       stepUpEnabled_ = true;
       entityCollisionEnabled_ = true;
      }
      if (d.contains("render") && d["render"].is_object()) {
       const json& r = d["render"];
       renderSettings_.greedyMeshing = r.value("greedy_meshing", true);
       renderSettings_.faceQuads = r.value("face_quads", true);
       renderSettings_.frustumCulling = r.value("frustum_culling", true);
       renderSettings_.batchCache = r.value("batch_cache", true);
       renderSettings_.creatureDebugBounds = r.value("creature_debug_bounds", false);
       renderSettings_.creatureTexturedParts = r.value("creature_textured_parts", true);
       renderSettings_.creatureWireframeOverlay = r.value("creature_wireframe_overlay", false);
       if (renderSettings_.greedyMeshing && !renderSettings_.faceQuads) {
        std::cout << "Render: greedy_meshing enabled — auto-enabling face_quads" << std::endl;
        renderSettings_.faceQuads = true;
       }
      } else {
       renderSettings_ = RenderSettings::Default();
      }
      if (d.contains("ui") && d["ui"].is_object()) {
       const json& u = d["ui"];
       uiSettings_.legacyHud = u.value("legacy_hud", false);
       uiSettings_.consoleKey = u.value("console_key", "grave");
       uiSettings_.paletteKey = u.value("palette_key", "b");
       uiSettings_.inventoryKey = u.value("inventory_key", "e");
       uiSettings_.hotbarCount = std::clamp(u.value("hotbar_count", 1), 1, 2);
       std::string schemeStr = "classic";
       if (u.contains("control_scheme") && u["control_scheme"].is_string()) {
        schemeStr = u["control_scheme"].get<std::string>();
       } else if (u.contains("block_input_profile") && u["block_input_profile"].is_string()) {
        schemeStr = u["block_input_profile"].get<std::string>();
       }
       uiSettings_.controlScheme = ControlSchemeFromString(schemeStr);
       uiSettings_.placeClickMaxSeconds = u.value("place_click_max_seconds", 0.20f);
       uiSettings_.breakHoldMinSeconds = u.value("break_hold_min_seconds", 0.50f);
       uiSettings_.breakDurationSeconds = u.value("break_duration_seconds", 0.25f);
       uiSettings_.rmbDragThresholdPx = u.value("rmb_drag_threshold_px", 4);
      }
     } else {
      default_world_name.clear();
      default_user_name = "Username";
      worldSeed_ = 12345u;
      terrainType_ = "heightmap";
      proceduralSettings_ = ProceduralSettings{};
      proceduralSettings_.seed = worldSeed_;
      renderDistanceChunks_ = 4;
      streamingEnabled_ = true;
      renderSettings_ = RenderSettings::Default();
     }

     texture_base_storage_file_name = project_dir / "textures" / "blocks";
     texture_cube_storage_file_name = project_dir / "models" / "blocks";
     object_storage_file_name = project_dir / "models" / "objects";
     prefabs_path_ = project_dir / "prefabs";

     auto blockDefinitions = std::make_shared<BlockDefinitionStorage>();
     blockDefinitions->Load(texture_cube_storage_file_name.string());
     TextureCubeStorageInstance->SetBlockDefinitions(blockDefinitions);
     WorldInstance->SetBlockDefinitionStorage(blockDefinitions);

     auto creatureDefinitions = std::make_shared<CreatureDefinitionStorage>();
     creatureDefinitions->Load((project_dir / "models" / "creatures").string());
     WorldInstance->SetCreatureDefinitionStorage(creatureDefinitions);

     auto skinDefinitions = std::make_shared<SkinDefinitionStorage>();
     skinDefinitions->Load((project_dir / "models" / "skins").string());
     WorldInstance->SetSkinDefinitionStorage(skinDefinitions);

     CreatureTextureStorageInstance = std::make_shared<CreatureTextureStorage>();
     CreatureTextureStorageInstance->LoadFromCreatureAndSkinRoots(
         (project_dir / "models" / "creatures").string(),
         (project_dir / "models" / "skins").string());

     TextureBaseStorageInstance->Load(texture_base_storage_file_name.string());

     TextureCubeStorageInstance->Load(texture_cube_storage_file_name.string());

     ObjectStorageInstance->Load(object_storage_file_name.string());

     WorldInstance->RefreshBlockRegistry();

     if (PrefabLibraryInstance) {
      PrefabLibraryInstance->Load(prefabs_path_.string(), WorldInstance->GetBlockRegistry());
      WorldInstance->SetPrefabLibrary(PrefabLibraryInstance.get());
      if (auto user = WorldInstance->GetCurrentUser()) {
       WorldInstance->EnsurePlayerHotbarCount(user, static_cast<size_t>(uiSettings_.hotbarCount));
      }
     }

     WorldInstance->SetProceduralSettings(proceduralSettings_);
     std::cout << "Procedural: " << ProceduralGeneratorToString(proceduralSettings_.generator)
               << " (" << VerticalModeToString(proceduralSettings_.vertical)
               << ", seed=" << proceduralSettings_.seed
               << ", sea=" << proceduralSettings_.seaLevel
               << ", maxY=" << proceduralSettings_.maxHeight
               << ", caves=" << (proceduralSettings_.enableCaves ? "1" : "0")
               << ", trees=" << (proceduralSettings_.enableTrees ? "1" : "0") << ")" << std::endl;
     WorldInstance->SetStreamingEnabled(streamingEnabled_);
     WorldInstance->SetRenderDistanceChunks(renderDistanceChunks_);
     WorldInstance->SetStepUpEnabled(stepUpEnabled_);
     WorldInstance->SetEntityCollisionEnabled(entityCollisionEnabled_);
     WorldInstance->SetRenderSettings(renderSettings_);
     if (GeometryEngineInstance) {
      GeometryEngineInstance->SetRenderSettings(renderSettings_);
      GeometryEngineInstance->SetCreatureTextureStorage(CreatureTextureStorageInstance);
     }
     std::cout << "Render: greedy=" << renderSettings_.greedyMeshing
               << " face_quads=" << renderSettings_.faceQuads
               << " frustum=" << renderSettings_.frustumCulling
               << " batch_cache=" << renderSettings_.batchCache << std::endl;
     std::cout << "Gameplay: step_up=" << (stepUpEnabled_ ? "1" : "0")
               << " entity_collision=" << (entityCollisionEnabled_ ? "1" : "0") << std::endl;
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error: " << e.what() << std::endl;
 }
}

void UCore::EnterGame()
{
 try {
     std::filesystem::create_directories(WorldPath);
     LoadWorldList(WorldPath.string());

     if (ShouldCreateWorldOnStartup()) {
      if (!default_world_name.empty()) {
       std::cout << "Core::EnterGame: world '" << default_world_name
                 << "' not found, creating a new one." << std::endl;
      }
      CreateWorld();
      SaveSystem(configFilePath_.filename().string());
     } else {
      LoadLastWorld();
     }

     if (default_user_name.empty()) {
      default_user_name = WorldInstance->GetCurrentUserName();
     }
     if (WorldInstance->GetCurrentUser() == nullptr) {
      WorldInstance->GenerateUsers();
     }
     if (Creature* player = WorldInstance->GetPlayerCreature()) {
      if (player->GetInventory().GetActiveEntryRef() == nullptr) {
       player->GetInventory().SetActiveSlot(0, 1);
      }
     }
     std::cout << kCubatariumVersion << " (feet snap: BlockTopY)" << std::endl;
 } catch (const std::exception& e) {
     std::cerr << "Core::EnterGame error: " << e.what() << std::endl;
     CreateWorld();
 }
}

void UCore::LoadSystem(const std::string& config_file_name)
{
 LoadConfig(config_file_name);
 EnterGame();
}

void UCore::SaveConfigFile()
{
 if (configFilePath_.empty()) {
  configFilePath_ = exeDir_ / "config.json";
 }

 json system_data;
 system_data["default_world"] = default_world_name;
 system_data["default_user"] = default_user_name;
 system_data["world_seed"] = worldSeed_;
 WriteProceduralSettings(system_data, proceduralSettings_);
 system_data["render_distance_chunks"] = renderDistanceChunks_;
 system_data["streaming_enabled"] = streamingEnabled_;
 json gameplay;
 gameplay["step_up"] = stepUpEnabled_;
 gameplay["entity_collision"] = entityCollisionEnabled_;
 system_data["gameplay"] = gameplay;
 json render;
 render["greedy_meshing"] = renderSettings_.greedyMeshing;
 render["face_quads"] = renderSettings_.faceQuads;
 render["frustum_culling"] = renderSettings_.frustumCulling;
 render["batch_cache"] = renderSettings_.batchCache;
 render["creature_debug_bounds"] = renderSettings_.creatureDebugBounds;
 render["creature_textured_parts"] = renderSettings_.creatureTexturedParts;
 render["creature_wireframe_overlay"] = renderSettings_.creatureWireframeOverlay;
 system_data["render"] = render;
 WriteUiSettings(system_data, uiSettings_);

 std::ofstream file(configFilePath_.string());
 if (file.is_open()) {
  file << system_data.dump(4);
  file.close();
 } else {
  std::cerr << "Failed to write config: " << configFilePath_.string() << std::endl;
 }
}

void UCore::SaveSystem(const std::string& config_file_name)
{
 if (!WorldInstance->GetWorldName().empty()) {
  default_world_name = WorldInstance->GetWorldName();
 }
 if (!WorldInstance->GetCurrentUserName().empty()) {
  default_user_name = WorldInstance->GetCurrentUserName();
 }
 worldSeed_ = proceduralSettings_.seed;

 if (configFilePath_.empty()) {
  configFilePath_ = exeDir_ / config_file_name;
 }

 SaveConfigFile();
 SaveWorld(WorldInstance->GetWorldName());
}

AppSettingsSnapshot UCore::GetAppSettings() const
{
 AppSettingsSnapshot snapshot;
 snapshot.defaultUser = default_user_name;
 snapshot.defaultWorld = default_world_name;
 snapshot.renderDistanceChunks = renderDistanceChunks_;
 snapshot.streamingEnabled = streamingEnabled_;
 snapshot.stepUpEnabled = stepUpEnabled_;
 snapshot.entityCollisionEnabled = entityCollisionEnabled_;
 snapshot.render = renderSettings_;
 snapshot.ui = uiSettings_;
 return snapshot;
}

void UCore::ApplyAppSettings(const AppSettingsSnapshot& settings)
{
 default_user_name = settings.defaultUser;
 default_world_name = settings.defaultWorld;
 renderDistanceChunks_ = settings.renderDistanceChunks;
 streamingEnabled_ = settings.streamingEnabled;
 stepUpEnabled_ = settings.stepUpEnabled;
 entityCollisionEnabled_ = settings.entityCollisionEnabled;
 renderSettings_ = settings.render;
 uiSettings_ = settings.ui;

 WorldInstance->SetStreamingEnabled(streamingEnabled_);
 WorldInstance->SetRenderDistanceChunks(renderDistanceChunks_);
 WorldInstance->SetStepUpEnabled(stepUpEnabled_);
 WorldInstance->SetEntityCollisionEnabled(entityCollisionEnabled_);
 WorldInstance->SetRenderSettings(renderSettings_);
 if (GeometryEngineInstance) {
  GeometryEngineInstance->SetRenderSettings(renderSettings_);
  GeometryEngineInstance->SetCreatureTextureStorage(CreatureTextureStorageInstance);
 }
}

void UCore::SetProceduralTemplate(const ProceduralSettings& settings)
{
 proceduralSettings_ = settings;
 worldSeed_ = settings.seed;
 terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);
 ResolveProceduralDefaults(proceduralSettings_);
 ApplyGeneratorTierDefaults(proceduralSettings_);
}

void UCore::CreateNewWorldFromTemplate()
{
 worldSeed_ += 1;
 proceduralSettings_.seed = worldSeed_;
 ResolveProceduralDefaults(proceduralSettings_);
 ApplyGeneratorTierDefaults(proceduralSettings_);
 terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);
 CreateNewWorldWithCurrentSettings();
}

void UCore::RefreshWorldList()
{
 std::filesystem::create_directories(WorldPath);
 LoadWorldList(WorldPath.string());
}

void UCore::LoadWorldByName(const std::string& world_name)
{
 default_world_name = world_name;
 LoadWorld(world_name);
}

void UCore::CreateWorld(const std::string& terrain_type)
{
 worldSeed_ += 1;
 if (!terrain_type.empty()) {
  terrainType_ = terrain_type;
  proceduralSettings_.generator = ProceduralGeneratorFromString(terrain_type);
 }
 proceduralSettings_.seed = worldSeed_;
 ResolveProceduralDefaults(proceduralSettings_);
 ApplyGeneratorTierDefaults(proceduralSettings_);
 terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);
 CreateNewWorldWithCurrentSettings();
}

void UCore::CreateWorldFromProceduralConfig()
{
 if (!configFilePath_.empty() && std::filesystem::exists(configFilePath_)) {
  std::ifstream file(configFilePath_.string());
  if (file.is_open()) {
   std::stringstream buffer;
   buffer << file.rdbuf();
   try {
    const json d = json::parse(buffer.str());
    proceduralSettings_ = ParseProceduralSettings(d);
    worldSeed_ = proceduralSettings_.seed;
   } catch (const json::exception& e) {
    std::cerr << "CreateWorldFromProceduralConfig: config parse error: " << e.what() << std::endl;
   }
  }
 }

 worldSeed_ += 1;
 proceduralSettings_.seed = worldSeed_;
 ResolveProceduralDefaults(proceduralSettings_);
 ApplyGeneratorTierDefaults(proceduralSettings_);
 terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);

 std::cout << "Core::CreateWorldFromProceduralConfig: " << terrainType_
           << " (" << VerticalModeToString(proceduralSettings_.vertical)
           << ", seed=" << proceduralSettings_.seed << ")" << std::endl;

 CreateNewWorldWithCurrentSettings();
}

void UCore::CreateNewWorldWithCurrentSettings()
{
 const std::string new_world_name = AllocateNextWorldName();
 default_world_name = new_world_name;
 activeWorldFolder_ = WorldFolderPath(new_world_name);
 std::filesystem::create_directories(activeWorldFolder_ / "chunks");

 std::cout << "Core::CreateWorld: new world '" << new_world_name << "' at "
           << activeWorldFolder_.string() << std::endl;

 WorldInstance->SetProceduralSettings(proceduralSettings_);
 WorldInstance->Create(new_world_name);
 if (WorldInstance->GetCurrentUser() == nullptr) {
  WorldInstance->GenerateUsers();
 }
 SaveWorld(new_world_name);
 LoadWorldList(WorldPath.string());
}

void UCore::LoadWorld(const std::string& world_name)
{
 activeWorldFolder_ = WorldFolderPath(world_name);
 WorldInstance->Load(activeWorldFolder_.string());
 if (WorldInstance->GetCurrentUser() == nullptr) {
  WorldInstance->GenerateUsers();
 }
}

void UCore::LoadLastWorld()
{
 if (default_world_name.empty()) {
  std::cerr << "Core::LoadLastWorld: default_world is not set in config." << std::endl;
  return;
 }

 std::cout << "Loading last world: " << default_world_name
           << " (user: " << default_user_name << ")" << std::endl;

 LoadWorld(default_world_name);

 if (!default_user_name.empty()) {
  if (!WorldInstance->SetCurrentUserName(default_user_name)) {
   std::cerr << "Core::LoadLastWorld: user '" << default_user_name
             << "' not found, using current user." << std::endl;
  }
 }

 WorldInstance->FinalizePlayerAfterWorldLoad();
}

void UCore::SaveWorld(const std::string& world_name)
{
 if (activeWorldFolder_.empty()) {
  activeWorldFolder_ = WorldFolderPath(world_name);
 }
 WorldInstance->Save(activeWorldFolder_.string());
}

void UCore::LoadWorldList(const std::string& world_path)
{
 WorldList.clear();

 if (!std::filesystem::exists(world_path) || !std::filesystem::is_directory(world_path)) {
  return;
 }

 try {
  for (const auto& entry : std::filesystem::directory_iterator(world_path)) {
   if (!entry.is_directory()) {
    continue;
   }
   const std::string name = entry.path().filename().string();
   if (std::find(WorldList.begin(), WorldList.end(), name) == WorldList.end()) {
    WorldList.push_back(name);
   }
  }
 } catch (const std::filesystem::filesystem_error& ex) {
  std::cerr << ex.what() << std::endl;
 }
}

}
