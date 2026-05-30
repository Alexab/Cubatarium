#ifndef WORLD_H
#define WORLD_H

#include <memory>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <limits>
#include <unordered_set>
#include <array>
#include <map>
#include <tuple>
#include "BlockWorld.h"
#include "BlockRegistry.h"
#include "ChunkMeshCache.h"
#include "ChunkStreamer.h"
#include "ChunkManager.h"

namespace cutum {

class ViewEngine;
class ObjectStorage;
class PrefabLibrary;
class User;
class Camera;

class World
{
public:
 World(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<ViewEngine> views);

 void GenerateUsers();

 std::string GetWorldName() const;
 void SetWorldName(const std::string& value);

 glm::vec3 GetSpawnPoint() const;
 void SetSpawnPoint(glm::vec3 value);

 void SetTerrainParams(uint32_t seed, const std::string& terrainType);
 uint32_t GetWorldSeed() const { return worldSeed_; }
 const std::string& GetTerrainType() const { return terrainType_; }

 void Create(const std::string& world_name);
 void Load(const std::string& world_folder_path);
 void Save(const std::string& world_folder_path);

 std::shared_ptr<User> GetUser(const std::string &name);
 bool AddUser(const std::string &name);
 void DelUser(const std::string &name);

 const std::string& GetCurrentUserName() const;
 std::shared_ptr<User> GetCurrentUser();
 bool SetCurrentUserName(const std::string& name);

 std::shared_ptr<Camera> GetUserCamera(const std::string& name);
 std::shared_ptr<Camera> GetCurrentUserCamera();

 const BlockWorld& GetBlockWorld() const { return blockWorld_; }
 BlockWorld& GetBlockWorld() { return blockWorld_; }
 BlockRegistry& GetBlockRegistry() { return *blockRegistry_; }
 const BlockRegistry& GetBlockRegistry() const { return *blockRegistry_; }

 void RefreshBlockRegistry();
 void ApplySpawnToCamera();
 void FinalizePlayerAfterWorldLoad();
 bool IsBlockWorldReady() const { return blockWorldReady_; }
 void InvalidateBlockMesh();
 const std::vector<FaceInstance>& GetBlockRenderInstances();

 bool AddObjectByView();
 bool DelObjectByView();

 bool AddObject(const std::string type_id, const glm::vec3 &position);

 bool PlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos);
 bool CanPlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos) const;
 std::optional<glm::ivec3> FindPrefabAnchorFromView(const glm::vec3& position, const glm::vec3& front) const;

 void SetPrefabLibrary(PrefabLibrary* library) { prefabLibrary_ = library; }

 bool CheckCollision(const glm::vec3& position, float size = 1.0) const;
 /// Moves from `from` toward `from + delta` in small steps to avoid tunneling through blocks.
 glm::vec3 ResolveMovement(const glm::vec3& from, const glm::vec3& delta, float size) const;
 void DoMovement();
 void UpdateIntersection(const glm::vec3& position, const glm::vec3& front);
 void UpdateStreaming();
 size_t GetRenderInstanceCount() const;
 uint64_t GetMeshRevision() const;
 size_t GetCachedBlockCount() const { return cachedBlockCount_; }

 bool GetIsIntersectionExists() const;
 size_t GetIntersectionObjectIndex() const;
 size_t GetIntersectionCubeIndex() const;

 bool GetIsBlockIntersectionExists() const { return hasIntersectionBlock_; }
 glm::ivec3 GetIntersectionBlockPos() const { return intersectionBlockPos_; }

 uint64_t GetDurationDoMovementMks() const;

 void SetStreamingEnabled(bool enabled) { streamingEnabled_ = enabled; }
 void SetRenderDistanceChunks(int distance);
 bool IsStreamingEnabled() const { return streamingEnabled_; }

 static bool HasPersistedTerrainOnDisk(const std::string& world_folder_path);

private:
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const;
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const;

 bool CheckPositionFree(const glm::vec3& position, float size=1.0) const;
 std::optional<glm::vec3> FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const;

 bool AddObjectByView(const glm::vec3& position, const glm::vec3& front);
 bool DelObjectByView(const glm::vec3& position, const glm::vec3& front);

 void LoadUsers(const std::string &file_name);
 void SaveUsers(const std::string &file_name);

 void MigrateObjectsFromJson(const std::string &file_name);

 void LoadBlocks(const std::string &file_name);
 void SaveBlocks(const std::string &file_name);
 void LoadChunks(const std::string &file_name);
 void SaveChunks(const std::string &file_name);
 void SaveChunkToFile(glm::ivec3 chunkCoord, const std::string& world_folder);
 /// Returns voxels placed (>=0), or -1 if the file could not be read/parsed.
 int LoadChunkFromFile(glm::ivec3 chunkCoord, const std::string& world_folder);
 void MigrateMonolithicChunksJson(const std::string& chunks_file, const std::string& world_folder);

 void LoadWorldData(const std::string &file_name);
 void SaveWorldData(const std::string &file_name);

 void GenerateWorldBlocks();
 void RebuildBlockMesh();
 void InitStreamerCallbacks();
 void ApplyUserToCamera(const std::shared_ptr<User>& user);
 bool IsReasonablePlayerPosition(const glm::vec3& position) const;
 void SanitizeUserPosition(const std::shared_ptr<User>& user);
 void EnsurePlayerOnGround();
 std::optional<int> FindHighestSolidY(int x, int z) const;
 void MarkBlockChunkDirty(glm::ivec3 blockPos);

 std::string WorldName;
 glm::vec3 SpawnPoint;
 std::string CurrentUserName;
 uint32_t worldSeed_{12345};
 std::string terrainType_{"heightmap"};
 size_t cachedBlockCount_{0};
 bool blockWorldReady_{false};
 int physicsSuspendFrames_{0};
 bool allowProceduralFill_{true};
 bool hasPersistedSave_{false};
 bool loadedFromChunkSave_{false};

 std::map<std::string, std::shared_ptr<User>> Users;

 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<ViewEngine> ViewInstance;
 PrefabLibrary* prefabLibrary_{nullptr};

 std::unique_ptr<BlockRegistry> blockRegistry_;
 BlockWorld blockWorld_;
 ChunkMeshCache meshCache_;
 std::unique_ptr<ChunkStreamer> streamer_;
 bool streamingEnabled_{true};
 int renderDistanceChunks_{4};
 std::unordered_set<glm::ivec3, IVec3Hash> modifiedChunks_;
 std::string worldFolderPath_;
 bool IsIntersectionExists;
 glm::vec3 Intersection;
 float IntersectionDistance;
 size_t IntersectionCubeIndex;
 int IntersectionCubeSide;
 size_t IntersectionObjectIndex;

 bool hasIntersectionBlock_{false};
 glm::ivec3 intersectionBlockPos_{0};

 uint64_t DurationDoMovementMks;
};

}

#endif // WORLD_H
