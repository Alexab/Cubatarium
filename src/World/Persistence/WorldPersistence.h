#ifndef WORLDPERSISTENCE_H
#define WORLDPERSISTENCE_H

#include "World/Chunks/ChunkManager.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/ChunkStorageTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UWorld;

class UWorldPersistence
{
public:
  UWorldPersistence();

  static bool HasPersistedTerrainOnDisk(const std::string &world_folder_path);

  void EnsureChunkIoInitialized();
  void SetChunkWriteFormat(ChunkWriteFormat format);
  ChunkWriteFormat GetChunkWriteFormat() const;

  UChunkStorageService &GetChunkStorage() { return *ChunkStorage; }
  const UChunkStorageService &GetChunkStorage() const { return *ChunkStorage; }

  const std::string &GetWorldFolderPath() const { return WorldFolderPath; }
  void SetWorldFolderPath(const std::string &path) { WorldFolderPath = path; }

  void LoadUsers(UWorld &world, const std::string &file_name);
  void SaveUsers(UWorld &world, const std::string &file_name);
  void LoadWorldData(UWorld &world, const std::string &file_name);
  void SaveWorldData(UWorld &world, const std::string &file_name);

  void TickAsyncChunkIo(UWorld &world);
  void RequestAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord);
  void RequestAsyncTerrainColumnSave(UWorld &world, glm::ivec3 ground_coord);
  bool IsTerrainColumnDiskLoadPending(glm::ivec3 ground_coord) const;

  int LoadTerrainColumn(glm::ivec3 coord, UBlockWorld &block_world,
                        UBlockRegistry &registry, int max_height);
  void SaveTerrainColumn(glm::ivec3 ground_coord, UBlockWorld &block_world,
                         UBlockRegistry &registry, int max_height);
  void LoadInitialTerrainColumns(UWorld &world, glm::vec3 spawn_point,
                                 int render_distance_chunks);

private:
  std::unique_ptr<UAsyncChunkIO> AsyncChunkIo;
  std::unique_ptr<UChunkStorageService> ChunkStorage;
  std::unordered_map<glm::ivec3, int, IVec3Hash> PendingAsyncColumnLoadSlices;
  std::unordered_map<glm::ivec3, int, IVec3Hash> PendingAsyncColumnSaveSlices;
  std::string WorldFolderPath;
};

} // namespace cutum

#endif // WORLDPERSISTENCE_H
