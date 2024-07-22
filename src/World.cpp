#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QFile>
#include <filesystem>
#include <iostream>
#include "World.h"
#include "Object.h"
#include "ObjectStorage.h"
#include "User.h"
#include "ViewEngine.h"

namespace cutum {

World::World(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<ViewEngine> views)
 : ObjectStorageInstance(object_storage)
 , ViewInstance(views)
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

QVector3D World::GetSpawnPoint() const
{
 return SpawnPoint;
}

void World::SetSpawnPoint(QVector3D value)
{
 SpawnPoint = value;
}

void World::Create(const std::string& world_name)
{
 Objects.clear();
 auto plane = ObjectStorageInstance->TakeObject("terrain_plane");
 Objects.emplace_back(plane);
 WorldName = world_name;
}

void World::Load(const std::string& world_folder_path)
{
 std::string objects_file_name = world_folder_path+"/objects.json";
 std::string users_file_name = world_folder_path+"/users.json";
 std::string world_data_file_name = world_folder_path+"/world_data.json";

 LoadWorldData(world_data_file_name);
 LoadUsers(users_file_name);
 LoadObjects(objects_file_name);
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

bool World::AddObject(const std::string type_id, const QVector3D &position)
{
 auto object = ObjectStorageInstance->TakeObject(type_id);
 if(object == nullptr)
  return false;

 object->SetPoseFromTranslation(position);
 AddObject(object);
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
  Objects.erase(Objects.begin() + index);
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
}

bool World::CheckRayIntersection(const QVector3D& position, const QVector3D& front, std::map<float, std::tuple<int, QVector3D, QVector3D, size_t, size_t>>& distance_map) const
{
 distance_map.clear();

 for(size_t i=0; i<Objects.size(); i++)
 {
  std::map<float, std::tuple<int, QVector3D, QVector3D, size_t>> object_distance_map;

  if(Objects[i]->CheckRayIntersection(position, front, object_distance_map))
  {
   for(auto I = object_distance_map.begin(); I != object_distance_map.end(); ++I)
   {
    float distance = I->first;
    distance_map[distance] = std::tuple<int, QVector3D, QVector3D, size_t, size_t>(std::get<0>(I->second),
                                                                                   std::get<1>(I->second),
                                                                                   std::get<2>(I->second),
                                                                                   std::get<3>(I->second),
                                                                                   i);
   }
  }
 }

 if(distance_map.empty())
  return false;

 return true;
}

bool World::CheckRayIntersection(const QVector3D& position, const QVector3D& front, QVector3D& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const
{
 std::map<float, std::tuple<int, QVector3D, QVector3D, size_t, size_t>> distance_map;

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

std::shared_ptr<Object> World::FindObjectByView(const QVector3D& position, const QVector3D& front)
{
 QVector3D intersecion;
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

bool World::CheckPositionFree(const QVector3D& position, float size) const
{
 for(size_t j=0; j<Objects.size(); j++)
 {
  auto& object = Objects[j];
  if(object->CheckCollision(position, size))
   return false;
 }
 return true;
}

std::optional<QVector3D> World::FindNearestFreeCubePosition(const QVector3D& position, const QVector3D& front) const
{
 std::optional<QVector3D> result;

 std::map<float, std::tuple<int, QVector3D, QVector3D, size_t, size_t>> distance_map;

 if(!CheckRayIntersection(position, front, distance_map))
  return result;

 size_t selected_object = std::get<4>(distance_map.begin()->second);
 size_t selected_cube = std::get<3>(distance_map.begin()->second);

 for(auto I = distance_map.begin(); I != distance_map.end(); ++I)
 {
  QVector3D res_position;

  QVector3D intersecion = std::get<2>(I->second);
  float distance = I->first;
  size_t cube_index = std::get<3>(I->second);
  size_t object_index = std::get<4>(I->second);
  int cube_side = std::get<0>(I->second);

  if(cube_index != selected_cube || object_index != selected_object)
   continue;

  auto & cube = Objects[object_index]->GetCubes()[cube_index];

  switch(cube_side)
  {
  case CubeSide::CUBE_SIDE_LEFT:
    res_position = cube->GetCenterPosition()+QVector3D(-1.0, 0.0, 0.0);
  break;

  case CubeSide::CUBE_SIDE_RIGHT:
    res_position = cube->GetCenterPosition()+QVector3D(1.0, 0.0, 0.0);
  break;

  case CubeSide::CUBE_SIDE_FAR:
    res_position = cube->GetCenterPosition()+QVector3D(0.0, 0.0, -1.0);
  break;

  case CubeSide::CUBE_SIDE_NEAR:
    res_position = cube->GetCenterPosition()+QVector3D(0.0, 0.0, 1.0);
  break;

  case CubeSide::CUBE_SIDE_TOP:
    res_position = cube->GetCenterPosition()+QVector3D(0.0, 1.0, 0.0);
  break;

  case CubeSide::CUBE_SIDE_BOTTOM:
    res_position = cube->GetCenterPosition()+QVector3D(0.0, -1.0, 0.0);
  break;

  default:
   res_position = cube->GetCenterPosition()+QVector3D(0.0, 1.0, 0.0);
  }

  if(CheckPositionFree(res_position, 1.0f))
  {
   if(!Cube::CheckCollision(res_position, 1.0f, position, 1.0f))
   {
    result = res_position;
    break;
   }
  }
 }

 return result; // TODO
}

bool World::AddObjectByView(const QVector3D& position, const QVector3D& front)
{
 auto object_pos = FindNearestFreeCubePosition(position, front);
 if(object_pos.has_value())
 {
  auto user = GetCurrentUser();
  if(!user)
   return false;

  if(AddObject(user->GetActiveObjectTypeName(), QVector3D(object_pos.value())))
   UpdateIntersection(position, front);
 }
 return false;
}

bool World::DelObjectByView(const QVector3D& position, const QVector3D& front)
{
 QVector3D intersecion;
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


bool World::CheckCollision(const QVector3D& position, float size) const
{
 for(auto & object : Objects)
 {
  if(object->CheckCollision(position, size))
   return true;
 }
 return false;
}

void World::LoadUsers(const std::string &file_name)
{
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::ReadOnly | QIODevice::Text);
 val = file.readAll();
 file.close();

 Users.clear();
 QJsonDocument d = QJsonDocument::fromJson(val.toUtf8());
 QJsonObject sett2 = d.object();
 for(auto I = sett2.begin() ; I != sett2.end(); ++I)
 {
  auto user_name = std::string(I.key().toLocal8Bit());
  auto user_data = I.value();

  auto position_object=user_data.toObject();
  auto position_value = position_object.value(QString("position"));

  if(position_value.isUndefined())
    continue;

  if(!position_value.isArray())
   continue;

  auto position_array = position_value.toArray();
  if(position_array.size() != 3)
   continue;

  QVector3D position(position_array[0].toDouble(),
                        position_array[1].toDouble(),
                        position_array[2].toDouble());

  AddUser(user_name);
  //GetUser(user_name)->SetPosition();
 }
}

void World::SaveUsers(const std::string &file_name)
{
 QJsonObject objects;

 for(auto I=Users.begin(); I!=Users.end(); ++I)
 {
  auto user_name = I->first;
  auto user_data = I->second;

  QVector3D position(0.0f, 0.0f, 0.0f);

  QJsonArray arr;
  arr.append(QJsonValue(position.x()));
  arr.append(QJsonValue(position.y()));
  arr.append(QJsonValue(position.z()));

  QJsonObject user;
  user.insert("position", arr);

  objects.insert(user_name.c_str(),user);
 }

 QJsonDocument jsonDoc;
 jsonDoc.setObject(objects);
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::WriteOnly | QIODevice::Text);
 file.resize(0);
 val = file.write(jsonDoc.toJson());
 file.close();
}

void World::LoadObjects(const std::string &file_name)
{
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::ReadOnly | QIODevice::Text);
 val = file.readAll();
 file.close();

 QJsonDocument d = QJsonDocument::fromJson(val.toUtf8());
 QJsonArray objects = d.array();

 Objects.clear();
 for(int i =0; i<objects.size();i++)
 {
  auto object_data = objects[i].toObject();

  auto id_value=object_data.value("id");
  auto type_name_value=object_data.value("type_name");
  auto position_value = object_data.value(QString("position"));

  if(id_value.isUndefined() || type_name_value.isUndefined() || position_value.isUndefined())
   continue;

  if(!position_value.isArray())
   continue;

  auto position_array = position_value.toArray();
  if(position_array.size() != 3)
   continue;

  QVector3D position(position_array[0].toDouble(),
                        position_array[1].toDouble(),
                        position_array[2].toDouble());

  std::string object_type_name = type_name_value.toString().toLocal8Bit();
  AddObject(object_type_name, position);
 }
}

void World::SaveObjects(const std::string &file_name)
{
 QJsonArray objects;

 for(auto & object : Objects)
 {

  auto pose = object->GetPose();
  QVector3D position(pose(0,3), pose(1,3), pose(2,3));

  QJsonArray arr;
  arr.append(QJsonValue(position.x()));
  arr.append(QJsonValue(position.y()));
  arr.append(QJsonValue(position.z()));

  QJsonObject obj;
  obj.insert("id", QJsonValue(int(object->GetObjectId())));
  obj.insert("type_name", QJsonValue(ObjectStorageInstance->GetObjectTypeName(object->GetObjectTypeId()).c_str()));
  obj.insert("position", arr);

  objects.append(obj);
 }

 QJsonDocument jsonDoc;
 jsonDoc.setArray(objects);
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::WriteOnly | QIODevice::Text);
 file.resize(0);
 val = file.write(jsonDoc.toJson());
 file.close();
}

void World::LoadWorldData(const std::string &file_name)
{
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::ReadOnly | QIODevice::Text);
 val = file.readAll();
 file.close();
 qWarning() << val;
 QJsonDocument d = QJsonDocument::fromJson(val.toUtf8());
 QJsonObject sett2 = d.object();
 QJsonValue world_name_value = sett2.value(QString("world_name"));
 QJsonValue spawn_point_value = sett2.value(QString("spawn_point"));

 if(world_name_value.isUndefined() || spawn_point_value.isUndefined())
  return;

 if(!world_name_value.isString())
  return;
 std::string name = world_name_value.toString().toLocal8Bit();

 if(!spawn_point_value.isArray())
  return;

 auto spawn_point_array = spawn_point_value.toArray();
 if(spawn_point_array.size() != 3)
  return;

 QVector3D spawn_point(spawn_point_array[0].toDouble(),
                       spawn_point_array[1].toDouble(),
                       spawn_point_array[2].toDouble());

 WorldName = name;
 SpawnPoint = spawn_point;
}

void World::SaveWorldData(const std::string &file_name)
{
 QJsonObject world_data;

 world_data.insert("world_name", QJsonValue(WorldName.c_str()));

 QJsonArray arr;
 arr.append(QJsonValue(SpawnPoint.x()));
 arr.append(QJsonValue(SpawnPoint.y()));
 arr.append(QJsonValue(SpawnPoint.z()));
 world_data.insert("spawn_point", arr);

 QJsonDocument jsonDoc;
 jsonDoc.setObject(world_data);
 QString val;
 QFile file;
 file.setFileName(file_name.c_str());
 file.open(QIODevice::WriteOnly | QIODevice::Text);
 file.resize(0);
 val = file.write(jsonDoc.toJson());
 file.close();
}


void World::DoMovement()
{
 bool is_moved=GetCurrentUserCamera()->DoMovement(this);

 if(is_moved)
   UpdateIntersection(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
}

void World::UpdateIntersection(const QVector3D& position, const QVector3D& front)
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

}
