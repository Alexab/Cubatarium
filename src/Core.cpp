//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>
#include "Core.h"
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

size_t CountChunkJsonFiles(const std::filesystem::path& world_folder)
{
 const auto chunks_dir = world_folder / "chunks";
 if (!std::filesystem::exists(chunks_dir) || !std::filesystem::is_directory(chunks_dir)) {
  return 0;
 }
 size_t count = 0;
 for (const auto& entry : std::filesystem::directory_iterator(chunks_dir)) {
  if (entry.path().extension() == ".json") {
   ++count;
  }
 }
 return count;
}

std::filesystem::path ResolveWorldFolder(const std::filesystem::path& worlds_root,
                                         const std::string& world_name)
{
 std::vector<std::filesystem::path> candidates;
 candidates.push_back(worlds_root / world_name);

 const auto worldsParent = worlds_root.parent_path();
 if (!worldsParent.empty()) {
  candidates.push_back(worldsParent / "bin" / "worlds" / world_name);
  if (worlds_root.filename() == "worlds") {
   const auto projectRoot = worldsParent;
   candidates.push_back(projectRoot / "worlds" / world_name);
  }
 }

 std::filesystem::path best = candidates.front();
 size_t bestChunks = 0;
 for (const auto& candidate : candidates) {
  if (!std::filesystem::exists(candidate)) {
   continue;
  }
  const size_t chunkCount = CountChunkJsonFiles(candidate);
  if (chunkCount > bestChunks) {
   bestChunks = chunkCount;
   best = candidate;
  }
 }

 if (bestChunks > 0) {
  std::cout << "Using world folder: " << best.string()
            << " (" << bestChunks << " chunk files)" << std::endl;
 }
 return best;
}

bool HasPersistedWorld(const std::filesystem::path& worlds_root, const std::string& world_name)
{
 const auto folder = ResolveWorldFolder(worlds_root, world_name);
 if (!std::filesystem::exists(folder)) {
  return false;
 }
 return World::HasPersistedTerrainOnDisk(folder.string());
}

std::filesystem::path FindConfigPath(const std::filesystem::path& project_dir,
                                     const std::filesystem::path& cwd,
                                     const std::string& config_file_name)
{
 const std::filesystem::path candidates[] = {
     project_dir / config_file_name,
     cwd / config_file_name,
     project_dir / "bin" / config_file_name,
 };
 for (const auto& path : candidates) {
  if (std::filesystem::exists(path)) {
   return path;
  }
 }
 return project_dir / config_file_name;
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

void Core::LoadSystem(const std::string& config_file_name)
{
 const auto cwd = std::filesystem::current_path();
 WorkDir = cwd;

 const auto project_dir = FindProjectRoot(cwd);
 configFilePath_ = FindConfigPath(project_dir, cwd, config_file_name);

 std::string val;
 std::ifstream file(configFilePath_.string());
 if (!file.is_open()) {
  std::cerr << "Failed to open config file: " << configFilePath_.string() << std::endl;
  return;
 }
 std::stringstream buffer;
 buffer << file.rdbuf();
 val = buffer.str();
 file.close();

 try {
     json d = json::parse(val);
     std::string default_world_value = d.value("default_world", "");
     std::string default_user_value = d.value("default_user", "");
     worldSeed_ = d.value("world_seed", 12345u);
     terrainType_ = d.value("terrain", "heightmap");
     renderDistanceChunks_ = d.value("render_distance_chunks", 4);
     streamingEnabled_ = d.value("streaming_enabled", false);

     WorkDir = project_dir;

     default_world_name = default_world_value;
     default_user_name = default_user_value;

     texture_base_storage_file_name = project_dir;
     texture_base_storage_file_name.append("textures").append("blocks");
     texture_cube_storage_file_name = project_dir;
     texture_cube_storage_file_name.append("models").append("blocks");
     object_storage_file_name = project_dir;
     object_storage_file_name.append("models").append("objects");
     prefabs_path_ = project_dir;
     prefabs_path_.append("prefabs");
     WorldPath = project_dir;
     WorldPath.append("worlds");

     TextureBaseStorageInstance->Load(texture_base_storage_file_name.string());

     TextureCubeStorageInstance->Load(texture_cube_storage_file_name.string());

     ObjectStorageInstance->Load(object_storage_file_name.string());

     WorldInstance->RefreshBlockRegistry();

     if (PrefabLibraryInstance) {
      PrefabLibraryInstance->Load(prefabs_path_.string(), WorldInstance->GetBlockRegistry());
      WorldInstance->SetPrefabLibrary(PrefabLibraryInstance.get());
     }

     WorldInstance->SetTerrainParams(worldSeed_, terrainType_);
     WorldInstance->SetStreamingEnabled(streamingEnabled_);
     WorldInstance->SetRenderDistanceChunks(renderDistanceChunks_);

     LoadWorldList(WorldPath.string());

     const bool hasLastWorldConfig = !default_world_value.empty() && !default_user_value.empty();
     const bool lastWorldExists = hasLastWorldConfig
         && HasPersistedWorld(WorldPath, default_world_value);

     bool is_need_autocreate = false;
     if (!hasLastWorldConfig) {
      is_need_autocreate = true;
     } else if (lastWorldExists) {
      is_need_autocreate = false;
     } else if (WorldList.empty()) {
      is_need_autocreate = true;
     } else {
      std::cerr << "Last world '" << default_world_value
                << "' not found on disk, creating a new one." << std::endl;
      is_need_autocreate = true;
     }

     if (is_need_autocreate) {
      const std::string new_world_name =
          default_world_value.empty() ? "World" : default_world_value;
      const auto existing_folder = ResolveWorldFolder(WorldPath, new_world_name);
      if (World::HasPersistedTerrainOnDisk(existing_folder.string())) {
       std::cout << "Core::LoadSystem: found saved terrain at "
                 << existing_folder.string() << ", loading instead of creating."
                 << std::endl;
       LoadLastWorld();
      } else {
       activeWorldFolder_ = WorldPath / new_world_name;
       CreateWorld(new_world_name);
       SaveSystem(config_file_name);
      }
     } else {
      LoadLastWorld();
     }
     if (auto user = WorldInstance->GetCurrentUser()) {
      if (user->GetActiveObject() == nullptr) {
       user->SetActiveObjectTypeName("grass");
      }
     }
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error: " << e.what() << std::endl;
     CreateWorld("World");
     SaveSystem(config_file_name);
 }
}

void Core::SaveSystem(const std::string& config_file_name)
{
 json system_data;

 system_data["default_world"] = WorldInstance->GetWorldName();
 system_data["default_user"] = WorldInstance->GetCurrentUserName();
 system_data["world_seed"] = worldSeed_;
 system_data["terrain"] = terrainType_;
 system_data["render_distance_chunks"] = renderDistanceChunks_;
 system_data["streaming_enabled"] = streamingEnabled_;

 if (configFilePath_.empty()) {
  configFilePath_ = WorkDir / config_file_name;
 }

 std::ofstream file(configFilePath_.string());
 if (file.is_open()) {
  file << system_data.dump(4);
  file.close();
 }

 const auto bin_config = WorkDir / "bin" / config_file_name;
 if (bin_config != configFilePath_) {
  std::ofstream binFile(bin_config.string());
  if (binFile.is_open()) {
   binFile << system_data.dump(4);
   binFile.close();
  }
 }

 SaveWorld(WorldInstance->GetWorldName());
}

void Core::CreateWorld(const std::string& world_name, const std::string& terrain_type)
{
 worldSeed_ += 1;
 if (!terrain_type.empty()) {
  terrainType_ = terrain_type;
 }
 if(WorldInstance->GetCurrentUser() == nullptr)
 {
  WorldInstance->GenerateUsers();
 }
 activeWorldFolder_ = ResolveWorldFolder(WorldPath, world_name);
 WorldInstance->SetTerrainParams(worldSeed_, terrainType_);
 WorldInstance->Create(world_name);
 SaveWorld(world_name);
}

void Core::LoadWorld(const std::string& world_name)
{
 activeWorldFolder_ = ResolveWorldFolder(WorldPath, world_name);
 WorldInstance->Load(activeWorldFolder_.string());
 if(WorldInstance->GetCurrentUser() == nullptr)
 {
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
  activeWorldFolder_ = ResolveWorldFolder(WorldPath, world_name);
 }
 WorldInstance->Save(activeWorldFolder_.string());
}

void Core::LoadWorldList(const std::string& world_path)
{
 WorldList.clear();

 const auto addWorldsFrom = [this](const std::filesystem::path& root) {
  if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
   return;
  }
  try {
   for (const auto& entry : std::filesystem::directory_iterator(root)) {
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
 };

 const std::filesystem::path worlds_root(world_path);
 addWorldsFrom(worlds_root);
 if (worlds_root.filename() == "worlds") {
  addWorldsFrom(worlds_root.parent_path() / "bin" / "worlds");
 }
}

}
