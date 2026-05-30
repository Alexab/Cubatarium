//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <algorithm>
#include <cctype>
#include <cstdio>
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
#include "ProceduralConfigIO.h"
#include "ProceduralSettings.h"
#include "World.h"
#include "ProceduralConfigIO.h"
#include "ProceduralSettings.h"
#include "World.h"
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

std::filesystem::path FindProjectRoot(std::filesystem::path start)
{
 const auto startPath = start;
 for (int depth = 0; depth < kMaxProjectRootSearchDepth; ++depth) {
  const auto textures_dir = start / "textures" / "blocks";
  const auto models_blocks_dir = start / "models" / "blocks";
  const auto prefabs_dir = start / "prefabs";
  const bool hasTextures = std::filesystem::exists(textures_dir);
  const bool hasModels = std::filesystem::exists(models_blocks_dir);
  const bool hasPrefabs = std::filesystem::exists(prefabs_dir);
  if (hasTextures && hasModels && hasPrefabs) {
   return start;
  }
  if (!start.has_parent_path()) {
   break;
  }
  start = start.parent_path();
 }
 return startPath;
}

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

Core::Core(std::shared_ptr<TextureBaseStorage> texture_base_storage_,
           std::shared_ptr<TextureCubeStorage> texture_cube_storage_,
           std::shared_ptr<ObjectStorage> object_storage_,
           std::shared_ptr<PrefabLibrary> prefab_library_,
           std::shared_ptr<World> world_,
           std::shared_ptr<GeometryEngine> geometries_,
           std::shared_ptr<ViewEngine> views_)
 : TextureBaseStorageInstance(texture_base_storage_)
 , TextureCubeStorageInstance(texture_cube_storage_)
 , ObjectStorageInstance(object_storage_)
 , PrefabLibraryInstance(prefab_library_)
 , WorldInstance(world_)
 , GeometryEngineInstance(geometries_)
 , ViewEngineInstance(views_)
{
}

std::filesystem::path Core::WorldFolderPath(const std::string& world_name) const
{
 return WorldPath / world_name;
}

std::string Core::AllocateNextWorldName() const
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

bool Core::ShouldCreateWorldOnStartup() const
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

void Core::LoadSystem(const std::string& config_file_name)
{
 const auto cwd = std::filesystem::current_path();
 const auto project_dir = FindProjectRoot(cwd);

 exeDir_ = GetExecutableDirectory();
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
      if (d.contains("render") && d["render"].is_object()) {
       const json& r = d["render"];
       renderSettings_.greedyMeshing = r.value("greedy_meshing", false);
       renderSettings_.faceQuads = r.value("face_quads", false);
       renderSettings_.frustumCulling = r.value("frustum_culling", false);
       renderSettings_.batchCache = r.value("batch_cache", false);
       if (renderSettings_.greedyMeshing && !renderSettings_.faceQuads) {
        std::cout << "Render: greedy_meshing enabled — auto-enabling face_quads" << std::endl;
        renderSettings_.faceQuads = true;
       }
      } else {
       renderSettings_ = RenderSettings::Legacy();
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
      renderSettings_ = RenderSettings::Legacy();
     }

     texture_base_storage_file_name = project_dir / "textures" / "blocks";
     texture_cube_storage_file_name = project_dir / "models" / "blocks";
     object_storage_file_name = project_dir / "models" / "objects";
     prefabs_path_ = project_dir / "prefabs";

     TextureBaseStorageInstance->Load(texture_base_storage_file_name.string());

     TextureCubeStorageInstance->Load(texture_cube_storage_file_name.string());

     ObjectStorageInstance->Load(object_storage_file_name.string());

     WorldInstance->RefreshBlockRegistry();

     if (PrefabLibraryInstance) {
      PrefabLibraryInstance->Load(prefabs_path_.string(), WorldInstance->GetBlockRegistry());
      WorldInstance->SetPrefabLibrary(PrefabLibraryInstance.get());
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
     WorldInstance->SetRenderSettings(renderSettings_);
     GeometryEngineInstance->SetRenderSettings(renderSettings_);
     std::cout << "Render: greedy=" << renderSettings_.greedyMeshing
               << " face_quads=" << renderSettings_.faceQuads
               << " frustum=" << renderSettings_.frustumCulling
               << " batch_cache=" << renderSettings_.batchCache << std::endl;

     std::filesystem::create_directories(WorldPath);
     LoadWorldList(WorldPath.string());

     if (ShouldCreateWorldOnStartup()) {
      if (!default_world_name.empty()) {
       std::cout << "Core::LoadSystem: world '" << default_world_name
                 << "' not found, creating a new one." << std::endl;
      }
      CreateWorld();
      SaveSystem(config_file_name);
     } else {
      LoadLastWorld();
     }

     if (default_user_name.empty()) {
      default_user_name = WorldInstance->GetCurrentUserName();
     }
     if (auto user = WorldInstance->GetCurrentUser()) {
      if (user->GetActiveObject() == nullptr) {
       user->SetActiveObjectTypeName("grass");
      }
     }
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error: " << e.what() << std::endl;
     CreateWorld();
     SaveSystem(config_file_name);
 }
}

void Core::SaveSystem(const std::string& config_file_name)
{
 json system_data;

 system_data["default_world"] = WorldInstance->GetWorldName();
 system_data["default_user"] = WorldInstance->GetCurrentUserName();
 system_data["world_seed"] = worldSeed_;
 WriteProceduralSettings(system_data, proceduralSettings_);
 system_data["render_distance_chunks"] = renderDistanceChunks_;
 system_data["streaming_enabled"] = streamingEnabled_;
 json render;
 render["greedy_meshing"] = renderSettings_.greedyMeshing;
 render["face_quads"] = renderSettings_.faceQuads;
 render["frustum_culling"] = renderSettings_.frustumCulling;
 render["batch_cache"] = renderSettings_.batchCache;
 system_data["render"] = render;

 if (configFilePath_.empty()) {
  configFilePath_ = exeDir_ / config_file_name;
 }

 std::ofstream file(configFilePath_.string());
 if (file.is_open()) {
  file << system_data.dump(4);
  file.close();
 } else {
  std::cerr << "Failed to write config: " << configFilePath_.string() << std::endl;
 }

 SaveWorld(WorldInstance->GetWorldName());
}

void Core::CreateWorld(const std::string& terrain_type)
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

void Core::CreateWorldFromProceduralConfig()
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

void Core::CreateNewWorldWithCurrentSettings()
{
 if (WorldInstance->GetCurrentUser() == nullptr) {
  WorldInstance->GenerateUsers();
 }

 const std::string new_world_name = AllocateNextWorldName();
 default_world_name = new_world_name;
 activeWorldFolder_ = WorldFolderPath(new_world_name);
 std::filesystem::create_directories(activeWorldFolder_ / "chunks");

 std::cout << "Core::CreateWorld: new world '" << new_world_name << "' at "
           << activeWorldFolder_.string() << std::endl;

 WorldInstance->SetProceduralSettings(proceduralSettings_);
 WorldInstance->Create(new_world_name);
 SaveWorld(new_world_name);
 LoadWorldList(WorldPath.string());
}

void Core::LoadWorld(const std::string& world_name)
{
 activeWorldFolder_ = WorldFolderPath(world_name);
 WorldInstance->Load(activeWorldFolder_.string());
 if (WorldInstance->GetCurrentUser() == nullptr) {
  WorldInstance->GenerateUsers();
 }
}

void Core::LoadLastWorld()
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

void Core::SaveWorld(const std::string& world_name)
{
 if (activeWorldFolder_.empty()) {
  activeWorldFolder_ = WorldFolderPath(world_name);
 }
 WorldInstance->Save(activeWorldFolder_.string());
}

void Core::LoadWorldList(const std::string& world_path)
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
