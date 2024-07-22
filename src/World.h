#ifndef WORLD_H
#define WORLD_H

#include <memory>
#include <optional>
#include <string>
#include <QVector3D>

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

 QVector3D GetSpawnPoint() const;
 void SetSpawnPoint(QVector3D value);

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

 bool AddObject(const std::string type_id, const QVector3D &position);
 void DelObject(std::shared_ptr<Object> object);
 void DelObject(size_t index);

 bool CheckCollision(const QVector3D& position, float size = 1.0) const;
 void DoMovement();
 void UpdateIntersection(const QVector3D& position, const QVector3D& front);

 bool GetIsIntersectionExists() const;
 size_t GetIntersectionObjectIndex() const;
 size_t GetIntersectionCubeIndex() const;

private:
 bool CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D, size_t, size_t>>& distance_map) const;
 bool CheckRayIntersection(const QVector3D& position, const QVector3D& front, QVector3D& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const;

 std::shared_ptr<Object> FindObjectByView(const QVector3D& position, const QVector3D& front);
 bool CheckPositionFree(const QVector3D& position, float size=1.0) const;
 std::optional<QVector3D> FindNearestFreeCubePosition(const QVector3D& position, const QVector3D& front) const;

private:
 void AddObject(std::shared_ptr<Object> object);
 bool AddObjectByView(const QVector3D& position, const QVector3D& front);
 bool DelObjectByView(const QVector3D& position, const QVector3D& front);

private:
 void LoadUsers(const std::string &file_name);
 void SaveUsers(const std::string &file_name);

 void LoadObjects(const std::string &file_name);
 void SaveObjects(const std::string &file_name);

 void LoadWorldData(const std::string &file_name);
 void SaveWorldData(const std::string &file_name);

private:
 std::string WorldName;

 QVector3D SpawnPoint;

 std::string CurrentUserName;

 std::map<std::string, std::shared_ptr<User>> Users;

 std::vector<std::shared_ptr<Object>> Objects;

 std::shared_ptr<ObjectStorage> ObjectStorageInstance;

 std::shared_ptr<ViewEngine> ViewInstance;


 bool IsIntersectionExists;
 QVector3D Intersection;
 float IntersectionDistance;
 size_t IntersectionCubeIndex;
 int IntersectionCubeSide;
 size_t IntersectionObjectIndex;
};

}

#endif // WORLD_H
