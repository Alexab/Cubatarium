#ifndef CORE_H
#define CORE_H

#include <map>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdint>

#include "RenderSettings.h"
#include "ProceduralSettings.h"

namespace cutum {

class World;
class TextureBaseStorage;
class TextureCubeStorage;
class ObjectStorage;
class PrefabLibrary;
class GeometryEngine;
class ViewEngine;

class Core
{
public:
 Core(std::shared_ptr<TextureBaseStorage> texture_base_storage_,
      std::shared_ptr<TextureCubeStorage> texture_cube_storage_,
      std::shared_ptr<ObjectStorage> object_storage_,
      std::shared_ptr<PrefabLibrary> prefab_library_,
      std::shared_ptr<World> world_,
      std::shared_ptr<GeometryEngine>
      geometries_,
      std::shared_ptr<ViewEngine> views_);

public:
 void LoadSystem(const std::string& config_file_name);
 void SaveSystem(const std::string& config_file_name);

 void CreateWorld(const std::string& terrain_type = "");
 void CreateWorldFromProceduralConfig();
 void LoadWorld(const std::string& world_name);
 void LoadLastWorld();
 void SaveWorld(const std::string& world_name);

 void LoadWorldList(const std::string& world_path);

 uint32_t GetWorldSeed() const { return worldSeed_; }
 const ProceduralSettings& GetProceduralSettings() const { return proceduralSettings_; }
 bool IsStepUpEnabled() const { return stepUpEnabled_; }

private:
 std::vector<std::string> WorldList;

 std::filesystem::path WorkDir;
 std::filesystem::path exeDir_;

 std::filesystem::path texture_base_storage_file_name;
 std::filesystem::path texture_cube_storage_file_name;
 std::filesystem::path object_storage_file_name;
 std::filesystem::path prefabs_path_;
 std::filesystem::path WorldPath;
 std::filesystem::path activeWorldFolder_;
 std::filesystem::path configFilePath_;

 std::string default_world_name;
 std::string default_user_name;

 uint32_t worldSeed_{12345};
 std::string terrainType_{"heightmap"};
 ProceduralSettings proceduralSettings_;
 int renderDistanceChunks_{4};
 bool streamingEnabled_{true};
 bool stepUpEnabled_{true};
 RenderSettings renderSettings_;

 std::shared_ptr<TextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<TextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<PrefabLibrary> PrefabLibraryInstance;
 std::shared_ptr<GeometryEngine> GeometryEngineInstance;
 std::shared_ptr<ViewEngine> ViewEngineInstance;
 std::shared_ptr<World> WorldInstance;

 bool ShouldCreateWorldOnStartup() const;
 std::filesystem::path WorldFolderPath(const std::string& world_name) const;
 std::string AllocateNextWorldName() const;
 void CreateNewWorldWithCurrentSettings();
};

}
#endif // CORE_H
