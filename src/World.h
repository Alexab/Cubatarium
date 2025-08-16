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

 glm::vec3 GetSpawnPoint() const; // QVector3D -> glm::vec3
 void SetSpawnPoint(glm::vec3 value); // QVector3D -> glm::vec3

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

 bool AddObjectByView();
 bool DelObjectByView();

 bool AddObject(const std::string type_id, const glm::vec3 &position); // QVector3D -> glm::vec3
 void DelObject(std::shared_ptr<Object> object);
 void DelObject(size_t index);

 bool CheckCollision(const glm::vec3& position, float size = 1.0) const; // QVector3D -> glm::vec3
 void DoMovement();
 void UpdateIntersection(const glm::vec3& position, const glm::vec3& front); // QVector3D -> glm::vec3

 bool GetIsIntersectionExists() const;
 size_t GetIntersectionObjectIndex() const;
 size_t GetIntersectionCubeIndex() const;

 uint64_t GetDurationDoMovementMks() const;

private:
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const; // QVector3D -> glm::vec3
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const; // QVector3D -> glm::vec3

 std::shared_ptr<Object> FindObjectByView(const glm::vec3& position, const glm::vec3& front); // QVector3D -> glm::vec3
 bool CheckPositionFree(const glm::vec3& position, float size=1.0) const; // QVector3D -> glm::vec3
 std::optional<glm::vec3> FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const; // QVector3D -> glm::vec3

     // Optimized methods using Octree
 std::vector<std::shared_ptr<Object>> GetObjectsInRadius(const glm::vec3& position, float radius) const; // QVector3D -> glm::vec3
 void UpdateSpatialIndex();
 void RebuildOctree();

private:
 void AddObject(std::shared_ptr<Object> object);
 bool AddObjectByView(const glm::vec3& position, const glm::vec3& front); // QVector3D -> glm::vec3
 bool DelObjectByView(const glm::vec3& position, const glm::vec3& front); // QVector3D -> glm::vec3

private:
 void LoadUsers(const std::string &file_name);
 void SaveUsers(const std::string &file_name);

 void LoadObjects(const std::string &file_name);
 void SaveObjects(const std::string &file_name);

 void LoadWorldData(const std::string &file_name);
 void SaveWorldData(const std::string &file_name);

private:
 std::string WorldName;

 glm::vec3 SpawnPoint; // QVector3D -> glm::vec3

 std::string CurrentUserName;

 std::map<std::string, std::shared_ptr<User>> Users;

 std::vector<std::shared_ptr<Object>> Objects;

 std::shared_ptr<ObjectStorage> ObjectStorageInstance;

 std::shared_ptr<ViewEngine> ViewInstance;

     // Spatial partitioning
 std::unique_ptr<OctreeNode> spatialIndex;
 bool spatialIndexDirty;

 bool IsIntersectionExists;
 glm::vec3 Intersection; // QVector3D -> glm::vec3
 float IntersectionDistance;
 size_t IntersectionCubeIndex;
 int IntersectionCubeSide;
 size_t IntersectionObjectIndex;

 uint64_t DurationDoMovementMks;
};

}

#endif // WORLD_H
