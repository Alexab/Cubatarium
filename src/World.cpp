//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "World.h"
#include "Object.h"
#include "ObjectStorage.h"
#include "User.h"
#include "ViewEngine.h"

using json = nlohmann::json;

namespace cutum {

World::World(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<ViewEngine> views)
 : ObjectStorageInstance(object_storage)
 , ViewInstance(views)
 , spatialIndexDirty(false)
{
 IsIntersectionExists = false;
}

void World::GenerateUsers()
{
 AddUser("Username");
 GetUser("Username")->SetActiveObjectTypeName("grass");
}

std::string World::GetWorldName() const
{
 return WorldName;
}

void World::SetWorldName(const std::string& value)
{
 WorldName = value;
}

glm::vec3 World::GetSpawnPoint() const
{
 return SpawnPoint;
}

void World::SetSpawnPoint(glm::vec3 value)
{
 SpawnPoint = value;
}

void World::Create(const std::string& world_name)
{
 std::cout << "World::Create: Creating world '" << world_name << "'" << std::endl;
 
 Objects.clear();
 auto plane = ObjectStorageInstance->TakeObject("terrain_plane");
 if (plane) {
     std::cout << "World::Create: Added terrain_plane object" << std::endl;
     Objects.emplace_back(plane);
 } else {
     std::cout << "World::Create: Failed to get terrain_plane object!" << std::endl;
 }
 
 WorldName = world_name;
 
 // Инициализировать пространственный индекс
 RebuildOctree();
 
 std::cout << "World::Create: Total objects: " << Objects.size() << std::endl;
}

void World::Load(const std::string& world_folder_path)
{
 std::string objects_file_name = world_folder_path+"/objects.json";
 std::string users_file_name = world_folder_path+"/users.json";
 std::string world_data_file_name = world_folder_path+"/world_data.json";

 LoadWorldData(world_data_file_name);
 LoadUsers(users_file_name);
 LoadObjects(objects_file_name);
 
 // Инициализировать пространственный индекс после загрузки объектов
 RebuildOctree();
}

void World::Save(const std::string& world_folder_path)
{
 std::filesystem::create_directories(world_folder_path);
 std::string objects_file_name = world_folder_path+"/objects.json";
 std::string users_file_name = world_folder_path+"/users.json";
 std::string world_data_file_name = world_folder_path+"/world_data.json";

 SaveObjects(objects_file_name);
 SaveUsers(users_file_name);
 SaveWorldData(world_data_file_name);
}

bool World::AddObject(const std::string type_id, const glm::vec3 &position)
{
 std::cout << "World::AddObject: Adding object of type '" << type_id << "' at position (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
 
 auto object = ObjectStorageInstance->TakeObject(type_id);
 if(object == nullptr) {
     std::cout << "World::AddObject: Failed to get object of type '" << type_id << "'" << std::endl;
     return false;
 }

 std::cout << "World::AddObject: Object created successfully" << std::endl;
 object->SetPoseFromTranslation(position);
 AddObject(object);
 
 std::cout << "World::AddObject: Total objects: " << Objects.size() << std::endl;
 return true;
}

void World::DelObject(std::shared_ptr<Object> object)
{
 auto I = std::find(Objects.begin(),Objects.end(), object);
 if(I != Objects.end())
  Objects.erase(I);
}

void World::DelObject(size_t index)
{
 if(index < Objects.size())
 {
  auto object = Objects[index];
  Objects.erase(Objects.begin() + index);
  
  // Обновить пространственный индекс
  if (spatialIndex) {
      spatialIndex->Remove(object);
  } else {
      spatialIndexDirty = true;
  }
 }
}

bool World::AddUser(const std::string &name)
{
 if(Users.find(name) != Users.end())
  return false;

 if(name.empty())
  return false;

 Users[name] = std::make_shared<User>();
 if(Users.size() == 1)
  SetCurrentUserName(name);

 return true;
}

void World::DelUser(const std::string &name)
{
 if(Users.find(name) == Users.end())
  return;

 Users.erase(name);
}

std::shared_ptr<User> World::GetUser(const std::string &name)
{
 auto I = Users.find(name);
 return (I != Users.end())?I->second:nullptr;
}

const std::string& World::GetCurrentUserName() const
{
 return CurrentUserName;
}

std::shared_ptr<User> World::GetCurrentUser()
{
 return GetUser(CurrentUserName);
}

bool World::SetCurrentUserName(const std::string& name)
{
 if(Users.find(name) == Users.end())
  return false;
 CurrentUserName = name;
 return true;
}

std::shared_ptr<Camera> World::GetUserCamera(const std::string& name)
{
 auto user = GetUser(name);
 if(user == nullptr)
  return nullptr;

 return ViewInstance->GetCamera(user->GetViewId());
}

std::shared_ptr<Camera> World::GetCurrentUserCamera()
{
 auto user = GetCurrentUser();
 if(user == nullptr)
  return nullptr;

 return ViewInstance->GetCamera(user->GetViewId());
}

const std::vector<std::shared_ptr<Object>>& World::GetObjects() const
{
 return Objects;
}

std::vector<std::shared_ptr<Object>>& World::GetObjects()
{
 return Objects;
}

bool World::AddObjectByView()
{
 return AddObjectByView(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
}

bool World::DelObjectByView()
{
 return DelObjectByView(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
}

void World::AddObject(std::shared_ptr<Object> object)
{
 Objects.emplace_back(object);
 
 // Обновить пространственный индекс
 if (spatialIndex) {
     spatialIndex->Insert(object);
 } else {
     spatialIndexDirty = true;
 }
}

bool World::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const
{
 distance_map.clear();

 // Использовать Octree для оптимизации поиска
 std::vector<std::shared_ptr<Object>> candidateObjects;
 if (spatialIndex) {
     spatialIndex->QueryRay(position, front, candidateObjects);
 } else {
     // Fallback к полному перебору если Octree не инициализирован
     candidateObjects = Objects;
 }

 for(size_t i=0; i<candidateObjects.size(); i++)
 {
     std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t>> object_distance_map;

  if(candidateObjects[i]->CheckRayIntersection(position, front, object_distance_map))
  {
   for(auto I = object_distance_map.begin(); I != object_distance_map.end(); ++I)
   {
    float distance = I->first;
    // Найти индекс объекта в основном массиве
    auto it = std::find(Objects.begin(), Objects.end(), candidateObjects[i]);
    size_t objectIndex = (it != Objects.end()) ? std::distance(Objects.begin(), it) : i;
    
         distance_map[distance] = std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>(std::get<0>(I->second),
                                                                                    std::get<1>(I->second),
                                                                                    std::get<2>(I->second),
                                                                                    std::get<3>(I->second),
                                                                                    objectIndex);
   }
  }
 }

 if(distance_map.empty())
  return false;

 return true;
}

bool World::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const
{
 std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>> distance_map;

 bool result = CheckRayIntersection(position, front, distance_map);
 if(result)
 {
  cube_side = std::get<0>(distance_map.begin()->second);
  intersecion = std::get<2>(distance_map.begin()->second);
  distance = distance_map.begin()->first;
  cube_index = std::get<3>(distance_map.begin()->second);
  object_index = std::get<4>(distance_map.begin()->second);

 }

 return result;
}

std::shared_ptr<Object> World::FindObjectByView(const glm::vec3& position, const glm::vec3& front)
{
 glm::vec3 intersecion;
 float distance;
 size_t cube_index;
 size_t object_index;
 int cube_side;
 if(CheckRayIntersection(position, front, intersecion, distance, cube_index, cube_side, object_index))
 {
  return Objects[object_index];
 }
 else
  return nullptr;
}

bool World::CheckPositionFree(const glm::vec3& position, float size) const
{
 // Использовать Octree для оптимизации поиска
 std::vector<std::shared_ptr<Object>> nearbyObjects;
 if (spatialIndex) {
     spatialIndex->Query(position, size, nearbyObjects);
 } else {
     // Fallback к полному перебору если Octree не инициализирован
     nearbyObjects = Objects;
 }

 for(auto& object : nearbyObjects)
 {
  if(object->CheckCollision(position, size))
   return false;
 }
 return true;
}

std::optional<glm::vec3> World::FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const
{
 std::optional<glm::vec3> result;

 std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>> distance_map;

 if(!CheckRayIntersection(position, front, distance_map))
  return result;

 size_t selected_object = std::get<4>(distance_map.begin()->second);
 size_t selected_cube = std::get<3>(distance_map.begin()->second);

 for(auto I = distance_map.begin(); I != distance_map.end(); ++I)
 {
  glm::vec3 res_position;

  glm::vec3 intersecion = std::get<2>(I->second);
  float distance = I->first;
  size_t cube_index = std::get<3>(I->second);
  size_t object_index = std::get<4>(I->second);
  int cube_side = std::get<0>(I->second);

  if(cube_index != selected_cube || object_index != selected_object)
   continue;

  auto & cube = Objects[object_index]->GetCubes()[cube_index];

  // Получить размер куба для правильного вычисления смещения
  float cubeSize = cube->GetSize();
  
     switch(cube_side)
   {
   case CubeSide::CUBE_SIDE_LEFT:
     res_position = cube->GetCenterPosition()+glm::vec3(-cubeSize, 0.0, 0.0);
   break;

   case CubeSide::CUBE_SIDE_RIGHT:
     res_position = cube->GetCenterPosition()+glm::vec3(cubeSize, 0.0, 0.0);
   break;

   case CubeSide::CUBE_SIDE_FAR:
     res_position = cube->GetCenterPosition()+glm::vec3(0.0, 0.0, -cubeSize);
   break;

   case CubeSide::CUBE_SIDE_NEAR:
     res_position = cube->GetCenterPosition()+glm::vec3(0.0, 0.0, cubeSize);
   break;

   case CubeSide::CUBE_SIDE_TOP:
     res_position = cube->GetCenterPosition()+glm::vec3(0.0, cubeSize, 0.0);
   break;

   case CubeSide::CUBE_SIDE_BOTTOM:
     res_position = cube->GetCenterPosition()+glm::vec3(0.0, -cubeSize, 0.0);
   break;

   default:
    res_position = cube->GetCenterPosition()+glm::vec3(0.0, cubeSize, 0.0);
   }
  
  if(CheckPositionFree(res_position, cubeSize))
  {
   if(!Cube::CheckCollision(res_position, cubeSize, position, 1.0f))
   {
    result = res_position;
    break;
   }
  }
 }

 return result; // TODO
}

bool World::AddObjectByView(const glm::vec3& position, const glm::vec3& front)
{
 auto object_pos = FindNearestFreeCubePosition(position, front);
 if(object_pos.has_value())
 {
  auto user = GetCurrentUser();
  if(!user)
   return false;

     if(AddObject(user->GetActiveObjectTypeName(), object_pos.value()))
  {
   UpdateIntersection(position, front);
   return true;
  }
 }
 return false;
}

bool World::DelObjectByView(const glm::vec3& position, const glm::vec3& front)
{
 glm::vec3 intersecion;
 float distance;
 size_t cube_index;
 size_t object_index;
 int cube_side;
 if(!CheckRayIntersection(position, front, intersecion, distance, cube_index, cube_side, object_index))
  return false;

 DelObject(object_index);
 UpdateIntersection(position, front);
 return true;
}


bool World::CheckCollision(const glm::vec3& position, float size) const
{
 // Использовать Octree для оптимизации поиска
 std::vector<std::shared_ptr<Object>> nearbyObjects;
 if (spatialIndex) {
     spatialIndex->Query(position, size, nearbyObjects);
 } else {
     // Fallback к полному перебору если Octree не инициализирован
     nearbyObjects = Objects;
 }

 for(auto & object : nearbyObjects)
 {
  if(object->CheckCollision(position, size))
   return true;
 }
 return false;
}

void World::LoadUsers(const std::string &file_name)
{
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
 } else {
     std::cerr << "Failed to open users file: " << file_name << std::endl;
     return;
 }

 try {
     Users.clear();
     json d = json::parse(val);
     for(auto I = d.begin() ; I != d.end(); ++I)
     {
      auto user_name = I.key();
      auto user_data = I.value();

      auto position_value = user_data.value("position", json::array());

      if(position_value.empty() || !position_value.is_array())
       continue;

      if(position_value.size() != 3)
       continue;

      glm::vec3 position(position_value[0].get<float>(),
                         position_value[1].get<float>(),
                         position_value[2].get<float>());

      AddUser(user_name);
      //GetUser(user_name)->SetPosition();
     }
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error in LoadUsers: " << e.what() << std::endl;
 }
}

void World::SaveUsers(const std::string &file_name)
{
 json objects;

 for(auto I=Users.begin(); I!=Users.end(); ++I)
 {
  auto user_name = I->first;
  auto user_data = I->second;

  glm::vec3 position(0.0f, 0.0f, 0.0f);

  json arr = json::array({position.x, position.y, position.z});

  json user;
  user["position"] = arr;

  objects[user_name] = user;
 }

 std::ofstream file(file_name);
 if (file.is_open()) {
     file << objects.dump(4);
     file.close();
 }
}

void World::LoadObjects(const std::string &file_name)
{
 std::cout << "World::LoadObjects: Loading from " << file_name << std::endl;
 
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     std::cout << "World::LoadObjects: File content length: " << val.length() << std::endl;
 } else {
     std::cerr << "Failed to open objects file: " << file_name << std::endl;
     return;
 }

 try {
     json d = json::parse(val);
     json objects = d;

     Objects.clear();
     std::cout << "World::LoadObjects: Parsing " << objects.size() << " objects" << std::endl;
     
     for(const auto& object_data : objects)
     {
      auto id_value = object_data.value("id", 0);
      auto type_name_value = object_data.value("type_name", "");
      auto position_value = object_data.value("position", json::array());
      
      std::cout << "World::LoadObjects: Object id=" << id_value << ", type=" << type_name_value << std::endl;

      if(id_value == 0 || type_name_value.empty() || position_value.empty())
       continue;

      if(!position_value.is_array())
       continue;

      if(position_value.size() != 3)
       continue;

      glm::vec3 position(position_value[0].get<float>(),
                         position_value[1].get<float>(),
                         position_value[2].get<float>());

      std::string object_type_name = type_name_value;
      AddObject(object_type_name, position);
     }
     
     // Обновить пространственный индекс после загрузки всех объектов
     spatialIndexDirty = true;
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error in LoadObjects: " << e.what() << std::endl;
 }
}

void World::SaveObjects(const std::string &file_name)
{
 json objects = json::array();

 for(auto & object : Objects)
 {

  auto pose = object->GetPose();
  glm::vec3 position(pose[3][0], pose[3][1], pose[3][2]);

  json arr = json::array({position.x, position.y, position.z});

  json obj;
  obj["id"] = int(object->GetObjectId());
  obj["type_name"] = ObjectStorageInstance->GetObjectTypeName(object->GetObjectTypeId());
  obj["position"] = arr;

  objects.push_back(obj);
 }

 std::ofstream file(file_name);
 if (file.is_open()) {
     file << objects.dump(4);
     file.close();
 }
}

void World::LoadWorldData(const std::string &file_name)
{
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     std::cout << val << std::endl;
 } else {
     std::cerr << "Failed to open world data file: " << file_name << std::endl;
     return;
 }

 try {
     json d = json::parse(val);
     std::string world_name_value = d.value("world_name", "");
     json spawn_point_value = d.value("spawn_point", json::array());

     if(world_name_value.empty() || spawn_point_value.empty())
      return;

     if(!spawn_point_value.is_array())
      return;

     if(spawn_point_value.size() != 3)
      return;

     glm::vec3 spawn_point(spawn_point_value[0].get<float>(),
                           spawn_point_value[1].get<float>(),
                           spawn_point_value[2].get<float>());

     WorldName = world_name_value;
     SpawnPoint = spawn_point;
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error in LoadWorldData: " << e.what() << std::endl;
 }
}

void World::SaveWorldData(const std::string &file_name)
{
 json world_data;

 world_data["world_name"] = WorldName;

 json arr = json::array({SpawnPoint.x, SpawnPoint.y, SpawnPoint.z});
 world_data["spawn_point"] = arr;

 std::ofstream file(file_name);
 if (file.is_open()) {
     file << world_data.dump(4);
     file.close();
 }
}


void World::DoMovement()
{
 auto t_begin = std::chrono::high_resolution_clock::now();

 bool is_moved=GetCurrentUserCamera()->DoMovement(this);

 if(is_moved)
   UpdateIntersection(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
   
 // Обновить пространственный индекс если он устарел
 UpdateSpatialIndex();
   
 auto t_end = std::chrono::high_resolution_clock::now();
 DurationDoMovementMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
}

void World::UpdateIntersection(const glm::vec3& position, const glm::vec3& front)
{
 IsIntersectionExists=CheckRayIntersection(position, front, Intersection, IntersectionDistance, IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);
}

bool World::GetIsIntersectionExists() const
{
 return IsIntersectionExists;
}

size_t World::GetIntersectionObjectIndex() const
{
 return IntersectionObjectIndex;
}

size_t World::GetIntersectionCubeIndex() const
{
 return IntersectionCubeIndex;
}

uint64_t World::GetDurationDoMovementMks() const
{
 return DurationDoMovementMks;
}

std::vector<std::shared_ptr<Object>> World::GetObjectsInRadius(const glm::vec3& position, float radius) const
{
 std::vector<std::shared_ptr<Object>> result;
 if (spatialIndex) {
     spatialIndex->Query(position, radius, result);
 }
 return result;
}

void World::UpdateSpatialIndex()
{
 if (spatialIndexDirty) {
     RebuildOctree();
     spatialIndexDirty = false;
 }
}

void World::RebuildOctree()
{
 // Очистить существующий индекс
 if (spatialIndex) {
     spatialIndex->Clear();
 }
 
 // Создать новый Octree с подходящим размером
 float worldSize = 1000.0f; // Размер мира
   spatialIndex = std::make_unique<OctreeNode>(glm::vec3(0, 0, 0), worldSize);
 
 // Добавить все объекты в индекс
 for (const auto& object : Objects) {
     spatialIndex->Insert(object);
 }
 
 spatialIndexDirty = false;
}

}
