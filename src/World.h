#ifndef WORLD_H
#define WORLD_H

#include <memory>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <map>
#include <tuple>
#include "BlockWorld.h"
#include "BlockRegistry.h"
#include "ChunkMeshCache.h"
#include "Octree.h"

namespace cutum {

class ViewEngine;
class ObjectStorage;
class Object;
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

 const std::vector<std::shared_ptr<Object>>& GetObjects() const;
 std::vector<std::shared_ptr<Object>>& GetObjects();

 const BlockWorld& GetBlockWorld() const { return blockWorld_; }
 BlockWorld& GetBlockWorld() { return blockWorld_; }
 BlockRegistry& GetBlockRegistry() { return *blockRegistry_; }
 const BlockRegistry& GetBlockRegistry() const { return *blockRegistry_; }

 void RefreshBlockRegistry();
 void ApplySpawnToCamera();
 void InvalidateBlockMesh();
 const std::vector<BlockInstance>& GetBlockRenderInstances();

 bool AddObjectByView();
 bool DelObjectByView();

 bool AddObject(const std::string type_id, const glm::vec3 &position);
 void DelObject(std::shared_ptr<Object> object);
 void DelObject(size_t index);

 bool CheckCollision(const glm::vec3& position, float size = 1.0) const;
 void DoMovement();
 void UpdateIntersection(const glm::vec3& position, const glm::vec3& front);

 bool GetIsIntersectionExists() const;
 size_t GetIntersectionObjectIndex() const;
 size_t GetIntersectionCubeIndex() const;

 bool GetIsBlockIntersectionExists() const { return hasIntersectionBlock_; }
 glm::ivec3 GetIntersectionBlockPos() const { return intersectionBlockPos_; }

 uint64_t GetDurationDoMovementMks() const;

private:
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const;
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const;

 std::shared_ptr<Object> FindObjectByView(const glm::vec3& position, const glm::vec3& front);
 bool CheckPositionFree(const glm::vec3& position, float size=1.0) const;
 std::optional<glm::vec3> FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const;

 std::vector<std::shared_ptr<Object>> GetObjectsInRadius(const glm::vec3& position, float radius) const;
 void UpdateSpatialIndex();
 void RebuildOctree();

 void AddObject(std::shared_ptr<Object> object);
 bool AddObjectByView(const glm::vec3& position, const glm::vec3& front);
 bool DelObjectByView(const glm::vec3& position, const glm::vec3& front);

 void LoadUsers(const std::string &file_name);
 void SaveUsers(const std::string &file_name);

 void LoadObjects(const std::string &file_name);
 void SaveObjects(const std::string &file_name);
 void MigrateObjectsFromJson(const std::string &file_name);

 void LoadBlocks(const std::string &file_name);
 void SaveBlocks(const std::string &file_name);
 void LoadChunks(const std::string &file_name);
 void SaveChunks(const std::string &file_name);

 void LoadWorldData(const std::string &file_name);
 void SaveWorldData(const std::string &file_name);

 void RebuildBlockMesh();
 void ApplyUserToCamera(const std::shared_ptr<User>& user);
 void MarkBlockChunkDirty(glm::ivec3 blockPos);

 std::string WorldName;

 glm::vec3 SpawnPoint;

 std::string CurrentUserName;

 std::map<std::string, std::shared_ptr<User>> Users;

 std::vector<std::shared_ptr<Object>> Objects;

 std::shared_ptr<ObjectStorage> ObjectStorageInstance;

 std::shared_ptr<ViewEngine> ViewInstance;

 std::unique_ptr<BlockRegistry> blockRegistry_;
 BlockWorld blockWorld_;
 ChunkMeshCache meshCache_;
 bool meshInstancesReady_{false};

 std::unique_ptr<OctreeNode> spatialIndex;
 bool spatialIndexDirty;

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
