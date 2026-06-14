#ifndef CORE_H
#define CORE_H

#include <map>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdint>

#include "AppSettingsSnapshot.h"
#include "RenderSettings.h"
#include "ProceduralSettings.h"
#include "UiSettings.h"
#include <vector>

namespace cutum {

std::filesystem::path GetExecutableDirectory();

class UWorld;
class UTextureBaseStorage;
class UTextureCubeStorage;
class UCreatureTextureStorage;
class UObjectStorage;
class UPrefabLibrary;
class UGeometryEngine;
class UViewEngine;

class UCore
{
public:
 UCore(std::shared_ptr<UTextureBaseStorage> texture_base_storage,
      std::shared_ptr<UTextureCubeStorage> texture_cube_storage,
      std::shared_ptr<UObjectStorage> object_storage,
      std::shared_ptr<UPrefabLibrary> prefab_library,
      std::shared_ptr<UWorld> world,
      std::shared_ptr<UGeometryEngine> geometries,
      std::shared_ptr<UViewEngine> views);

public:
 void LoadConfig(const std::string& config_file_name);
 void EnterGame();
 void LoadSystem(const std::string& config_file_name);
 void SaveSystem(const std::string& config_file_name);
 void SaveConfigFile();

 AppSettingsSnapshot GetAppSettings() const;
 void ApplyAppSettings(const AppSettingsSnapshot& settings);

 ProceduralSettings GetProceduralTemplate() const { return ProceduralTemplate; }
 void SetProceduralTemplate(const ProceduralSettings& settings);

 void CreateNewWorldFromTemplate();

 const std::vector<std::string>& GetWorldList() const { return WorldList; }
 void RefreshWorldList();
 void LoadWorldByName(const std::string& world_name);

 const UiSettings& GetUiSettings() const { return Ui; }
 RenderSettings GetRenderSettings() const { return Render; }

 void CreateWorld(const std::string& terrain_type = "");
 void CreateWorldFromProceduralConfig();
 void LoadWorld(const std::string& world_name);
 void LoadLastWorld();
 void SaveWorld(const std::string& world_name);

 void LoadWorldList(const std::string& world_path);

 uint32_t GetWorldSeed() const { return WorldSeed; }
 const ProceduralSettings& GetProceduralSettings() const { return ProceduralTemplate; }
 bool IsStepUpEnabled() const { return StepUpEnabled; }
 bool IsEntityCollisionEnabled() const { return EntityCollisionEnabled; }
 std::shared_ptr<UPrefabLibrary> GetPrefabLibrary() const { return PrefabLibraryInstance; }
 std::shared_ptr<UTextureCubeStorage> GetTextureCubeStorage() const {
  return TextureCubeStorageInstance;
 }
 std::shared_ptr<UCreatureTextureStorage> GetCreatureTextureStorage() const {
  return CreatureTextureStorageInstance;
 }

private:
 std::vector<std::string> WorldList;

 std::filesystem::path WorkDir;
 std::filesystem::path ExeDir;

 std::filesystem::path TextureBaseStorageFileName;
 std::filesystem::path TextureCubeStorageFileName;
 std::filesystem::path ObjectStorageFileName;
 std::filesystem::path PrefabsPath;
 std::filesystem::path WorldPath;
 std::filesystem::path ActiveWorldFolder;
 std::filesystem::path ConfigFilePath;

 std::string DefaultWorldName;
 std::string DefaultUserName;

 uint32_t WorldSeed{12345};
 std::string TerrainType{"heightmap"};
 ProceduralSettings ProceduralTemplate;
 int RenderDistanceChunks{4};
 bool StreamingEnabled{true};
 bool StepUpEnabled{true};
 bool EntityCollisionEnabled{true};
 RenderSettings Render;
 UiSettings Ui;

 std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
 std::shared_ptr<UTextureCubeStorage> TextureCubeStorageInstance;
 std::shared_ptr<UCreatureTextureStorage> CreatureTextureStorageInstance;
 std::shared_ptr<UObjectStorage> ObjectStorageInstance;
 std::shared_ptr<UPrefabLibrary> PrefabLibraryInstance;
 std::shared_ptr<UGeometryEngine> GeometryEngineInstance;
 std::shared_ptr<UViewEngine> ViewEngineInstance;
 std::shared_ptr<UWorld> WorldInstance;

 bool ShouldCreateWorldOnStartup() const;
 std::filesystem::path WorldFolderPath(const std::string& world_name) const;
 std::string AllocateNextWorldName() const;
 void CreateNewWorldWithCurrentSettings();
};

}
#endif // CORE_H
