#ifndef CORE_H
#define CORE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "App/CreateWorldCli.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/RenderSettings.h"
#include "App/Settings/UiSettings.h"
#include "Blocks/BlockDefinition.h"
#include "Game/Interfaces/IUGameContent.h"
#include <array>
#include "WorldGen/Core/ProceduralSettings.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include <functional>
#include <vector>

namespace cutum
{

std::filesystem::path GetExecutableDirectory();

class UWorld;
class UTextureBaseStorage;
class UTextureCubeStorage;
class UCreatureTextureStorage;
class UCreatureDefinitionStorage;
class UObjectLibrary;
class UGeometryEngine;
class UViewEngine;
class UBlockDefinitionStorage;
class UBlockMergeRegistry;
class UPlaceholderTextureCache;

class UCore : public IUGameContent
{
public:
  UCore(std::shared_ptr<UTextureBaseStorage> texture_base_storage,
        std::shared_ptr<UTextureCubeStorage> texture_cube_storage,
        std::shared_ptr<UObjectLibrary> object_library,
        std::shared_ptr<UWorld> world,
        std::shared_ptr<UGeometryEngine> geometries,
        std::shared_ptr<UViewEngine> views);

public:
  void LoadConfig(const std::string &config_file_name);
  void EnterGame();
  void LoadSystem(const std::string &config_file_name);
  void SaveSystem(const std::string &config_file_name);
  void SaveConfigFile();

  AppSettingsSnapshot GetAppSettings() const;
  void ApplyAppSettings(const AppSettingsSnapshot &settings);

  ProceduralSettings GetProceduralTemplate() const
  {
    return ProceduralTemplate;
  }
  void SetProceduralTemplate(const ProceduralSettings &settings);

  void CreateNewWorldFromTemplate();

  const std::vector<std::string> &GetWorldList() const { return WorldList; }
  void RefreshWorldList();
  void LoadWorldByName(const std::string &world_name);

  const UiSettings &GetUiSettings() const { return Ui; }
  RenderSettings GetRenderSettings() const { return Render; }

  void CreateWorld(const std::string &terrain_type = "");
  void CreateWorldFromProceduralConfig();
  void LoadWorld(const std::string &world_name);
  void LoadLastWorld();
  void SaveWorld(const std::string &world_name);

  void LoadWorldList(const std::string &world_path);

  uint32_t GetWorldSeed() const { return WorldSeed; }
  const ProceduralSettings &GetProceduralSettings() const
  {
    return ProceduralTemplate;
  }
  bool IsStepUpEnabled() const { return StepUpEnabled; }
  bool IsEntityCollisionEnabled() const { return EntityCollisionEnabled; }
  std::shared_ptr<UObjectLibrary> GetObjectLibrary() const
  {
    return ObjectLibraryInstance;
  }
  const UBlockDefinitionStorage &Blocks() const override;
  const UObjectLibrary &Objects() const override;
  const UCreatureDefinitionStorage &Creatures() const override;
  const WorldGenPack &ActiveWorldGenPack() const override;

  std::shared_ptr<UBlockDefinitionStorage> GetBlockDefinitionStorage() const
  {
    return BlockDefinitionsInstance;
  }
  std::shared_ptr<UBlockMergeRegistry> GetBlockMergeRegistry() const
  {
    return BlockMergeRegistryInstance;
  }
  bool RegisterRuntimeBlock(const BlockDefinition &def,
                            const std::array<std::string, 6> &textureStems);
  void BeginRuntimeBlockBatch();
  void EndRuntimeBlockBatch();
  std::shared_ptr<UTextureCubeStorage> GetTextureCubeStorage() const
  {
    return TextureCubeStorageInstance;
  }
  std::shared_ptr<UCreatureTextureStorage> GetCreatureTextureStorage() const
  {
    return CreatureTextureStorageInstance;
  }

  std::vector<std::string> GetDefaultEnabledResourcePacks() const;
  ResourcePackSelection GetDefaultResourcePackSelection() const;
  void SetDefaultEnabledResourcePacks(const std::vector<std::string> &ids);
  void SetDefaultResourcePackSelection(const ResourcePackSelection &selection);
  std::vector<std::string> GetActiveResourcePacksEnabled() const
  {
    return ActiveResourcePacksEnabled;
  }
  std::vector<InstalledPackInfo> ListInstalledResourcePacks() const;
  bool ApplyResourcePacks(const std::vector<std::string> &enabledIds);
  bool ApplyResourcePacks(const ResourcePackSelection &selection);
  void CreateNewWorldWithSettings(const ProceduralSettings &settings,
                                  const std::vector<std::string> &resourcePacks);
  void CreateNewWorldWithSettings(const ProceduralSettings &settings,
                                  const ResourcePackSelection &selection);
  bool CreateWorldHeadless(const CreateWorldCliArgs &args,
                           CreateWorldReport &report);
  std::vector<std::string>
  PeekWorldResourcePacks(const std::string &world_name) const;
  ResourcePackSelection GetCurrentWorldResourcePackSelection() const;
  bool ApplyResourcePacksToCurrentWorld(const ResourcePackSelection &selection);

  void ApplyRuntimeStreamingToWorld();

  void PrepareEnterGameWorldList();
  void PrepareStartupWorldCreation();
  void PrepareLoadWorld(const std::string &world_name);
  void FinalizeLoadedWorld();
  void FinalizeEnterGameSession();
  std::string SetupNewWorldForCreation();
  void ApplyNewWorldCreationRequest(const ProceduralSettings &settings,
                                    const ResourcePackSelection &selection);
  bool NeedsCreateWorldOnStartup() const;
  const std::filesystem::path &GetActiveWorldFolder() const
  {
    return ActiveWorldFolder;
  }
  void RefreshWorldListAfterSave();

private:
  std::vector<std::string> WorldList;

  std::filesystem::path WorkDir;
  std::filesystem::path ExeDir;

  std::filesystem::path TextureBaseStorageFileName;
  std::filesystem::path TextureCubeStorageFileName;
  std::filesystem::path ObjectsPath;
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
  std::string ChunkStorageFormat{"binary"};
  bool StepUpEnabled{true};
  bool EntityCollisionEnabled{true};
  RenderSettings Render;
  UiSettings Ui;
  ResourcePacksConfig ResourcePacks;
  std::vector<std::string> ActiveResourcePacksEnabled;
  std::vector<std::string> PendingNewWorldResourcePacks;
  ResourcePackSelection PendingNewWorldPackSelection;
  std::optional<ProceduralSettings> PendingNewWorldSettings;
  ResourcePackSelection ActivePackSelection;

  std::shared_ptr<UBlockDefinitionStorage> BlockDefinitionsInstance;
  std::shared_ptr<UBlockMergeRegistry> BlockMergeRegistryInstance;
  std::shared_ptr<UPlaceholderTextureCache> PlaceholderCacheInstance;
  std::shared_ptr<UTextureBaseStorage> TextureBaseStorageInstance;
  std::shared_ptr<UTextureCubeStorage> TextureCubeStorageInstance;
  std::shared_ptr<UCreatureTextureStorage> CreatureTextureStorageInstance;
  std::shared_ptr<UObjectLibrary> ObjectLibraryInstance;
  std::shared_ptr<UGeometryEngine> GeometryEngineInstance;
  std::shared_ptr<UViewEngine> ViewEngineInstance;
  std::shared_ptr<UWorld> WorldInstance;

  bool ShouldCreateWorldOnStartup() const;
  std::filesystem::path WorldFolderPath(const std::string &world_name) const;
  std::string AllocateNextWorldName() const;
  void CreateNewWorldWithCurrentSettings();
  void RebuildBlockTexturesFromMergeRegistry();
  void ApplyResourcePacksAfterWorldDataLoaded();
  void ReloadCreatureCatalog(const std::vector<ResourcePackManifest> &packs);
  std::vector<std::string>
  NormalizeEnabledPackIds(const std::vector<std::string> &requested) const;
  void FlushRuntimeBlockOverlay();
  int RuntimeBlockBatchDepth{0};
  bool RuntimeBlockFlushPending{false};
};

} // namespace cutum
#endif // CORE_H
