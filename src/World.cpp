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
#include "BlockRaycast.h"
#include "GridMath.h"
#include "WorldGenerator.h"
#include "ChunkManager.h"
#include "Chunk.h"
#include <map>

using json = nlohmann::json;

namespace cutum {

World::World(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<ViewEngine> views)
 : ObjectStorageInstance(object_storage)
 , ViewInstance(views)
 , spatialIndexDirty(false)
{
 if (ObjectStorageInstance && ObjectStorageInstance->GetTextureCubeStorage()) {
  blockRegistry_ = std::make_unique<BlockRegistry>(ObjectStorageInstance->GetTextureCubeStorage());
 }
 IsIntersectionExists = false;
 hasIntersectionBlock_ = false;
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
 Objects.clear();
 blockWorld_.Clear();
 if (blockRegistry_) {
  WorldGenerator::GenerateFlat(blockWorld_, *blockRegistry_, 16, 3);
 }
 WorldName = world_name;
 meshCache_.MarkAllDirty();
 meshInstancesReady_ = false;
 spatialIndexDirty = true;
}

void World::Load(const std::string& world_folder_path)
{
 const std::string users_file_name = world_folder_path + "/users.json";
 const std::string world_data_file_name = world_folder_path + "/world_data.json";
 const std::string chunks_file_name = world_folder_path + "/chunks.json";
 const std::string blocks_file_name = world_folder_path + "/blocks.json";
 const std::string objects_file_name = world_folder_path + "/objects.json";

 Objects.clear();
 blockWorld_.Clear();

 LoadWorldData(world_data_file_name);
 LoadUsers(users_file_name);

 if (std::filesystem::exists(chunks_file_name)) {
  LoadChunks(chunks_file_name);
 } else if (std::filesystem::exists(blocks_file_name)) {
  LoadBlocks(blocks_file_name);
 } else if (std::filesystem::exists(objects_file_name)) {
  MigrateObjectsFromJson(objects_file_name);
 }

 meshCache_.MarkAllDirty();
 meshInstancesReady_ = false;
 spatialIndexDirty = true;

 if (auto camera = GetCurrentUserCamera()) {
  camera->SetPosition(SpawnPoint);
 }
}

void World::Save(const std::string& world_folder_path)
{
 std::filesystem::create_directories(world_folder_path);
 const std::string users_file_name = world_folder_path + "/users.json";
 const std::string world_data_file_name = world_folder_path + "/world_data.json";
 const std::string chunks_file_name = world_folder_path + "/chunks.json";

 SaveChunks(chunks_file_name);
 SaveUsers(users_file_name);
 SaveWorldData(world_data_file_name);
}

bool World::AddObject(const std::string type_id, const glm::vec3 &position)
{
 if (!blockRegistry_) {
  return false;
 }
 const BlockId id = blockRegistry_->GetIdByTypeName(type_id);
 if (id == BLOCK_AIR) {
  std::cerr << "World::AddObject: Unknown block type '" << type_id << "'" << std::endl;
  return false;
 }
 const glm::ivec3 blockPos = WorldPosToBlock(position);
 if (!blockWorld_.IsAir(blockPos)) {
  return false;
 }
 blockWorld_.SetBlock(blockPos, id);
 MarkBlockChunkDirty(blockPos);
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
 const auto hit = RaycastSolidBlocks(blockWorld_, position, front);
 if (!hit) {
  return false;
 }
 const glm::vec3 hitCenter = BlockCenter(hit->blockPos);
 distance_map[hit->distance] = std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>(
     0,
     glm::vec3(hit->faceNormal),
     hitCenter,
     0,
     0);
 return true;
}

bool World::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const
{
 std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>> distance_map;

 const bool result = CheckRayIntersection(position, front, distance_map);
 if (result) {
  cube_side = std::get<0>(distance_map.begin()->second);
  intersecion = std::get<2>(distance_map.begin()->second);
  distance = distance_map.begin()->first;
  cube_index = 0;
  object_index = 0;
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

bool World::CheckPositionFree(const glm::vec3& position, float /*size*/) const
{
 return blockWorld_.IsAir(WorldPosToBlock(position));
}

std::optional<glm::vec3> World::FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const
{
 const auto hit = RaycastSolidBlocks(blockWorld_, position, front);
 if (!hit) {
  return std::nullopt;
 }

 glm::ivec3 normal = hit->faceNormal;
 if (normal == glm::ivec3(0)) {
  const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
  if (std::abs(toCamera.x) >= std::abs(toCamera.y) && std::abs(toCamera.x) >= std::abs(toCamera.z)) {
   normal.x = toCamera.x > 0.0f ? 1 : -1;
  } else if (std::abs(toCamera.y) >= std::abs(toCamera.z)) {
   normal.y = toCamera.y > 0.0f ? 1 : -1;
  } else {
   normal.z = toCamera.z > 0.0f ? 1 : -1;
  }
 }

 const glm::ivec3 placePos = hit->blockPos + normal;
 if (!blockWorld_.IsAir(placePos)) {
  return std::nullopt;
 }

 const glm::vec3 res_position = BlockCenter(placePos);
 if (!CheckPositionFree(res_position, 1.0f)) {
  return std::nullopt;
 }

 constexpr float kCameraRadius = 0.3f;
 if (Cube::CheckCollision(res_position, 1.0f, position, kCameraRadius * 2.0f)) {
  return std::nullopt;
 }

 return res_position;
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
 const auto hit = RaycastSolidBlocks(blockWorld_, position, front);
 if (!hit) {
  return false;
 }
 blockWorld_.SetBlock(hit->blockPos, BLOCK_AIR);
 MarkBlockChunkDirty(hit->blockPos);
 UpdateIntersection(position, front);
 return true;
}


bool World::CheckCollision(const glm::vec3& position, float size) const
{
 const glm::ivec3 center = WorldPosToBlock(position);
 const int radius = static_cast<int>(std::ceil(size * 0.5f));
 for (int dx = -radius; dx <= radius; ++dx) {
  for (int dy = -radius; dy <= radius; ++dy) {
   for (int dz = -radius; dz <= radius; ++dz) {
    const glm::ivec3 blockPos = center + glm::ivec3(dx, dy, dz);
    if (!blockWorld_.IsAir(blockPos)) {
     const glm::vec3 blockCenter = BlockCenter(blockPos);
     if (Cube::CheckCollision(blockCenter, 1.0f, position, size)) {
      return true;
     }
    }
   }
  }
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
      if (auto camera = GetUserCamera(user_name)) {
       camera->SetPosition(position);
      }
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
  if (auto camera = GetUserCamera(user_name)) {
   position = camera->GetPosition();
  }

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
 
 std::string val;
 std::ifstream file(file_name);
 if (file.is_open()) {
     std::stringstream buffer;
     buffer << file.rdbuf();
     val = buffer.str();
     file.close();
     
 } else {
     std::cerr << "Failed to open objects file: " << file_name << std::endl;
     return;
 }

 try {
     json d = json::parse(val);
     json objects = d;

     Objects.clear();
     
     for(const auto& object_data : objects)
     {
      auto id_value = object_data.value("id", 0);
      auto type_name_value = object_data.value("type_name", "");
      auto position_value = object_data.value("position", json::array());
      
      

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
     
     // Update spatial index after loading all objects
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
   
 // Update spatial index if it's outdated
 UpdateSpatialIndex();
   
 auto t_end = std::chrono::high_resolution_clock::now();
 DurationDoMovementMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
}

void World::UpdateIntersection(const glm::vec3& position, const glm::vec3& front)
{
 IsIntersectionExists = CheckRayIntersection(position, front, Intersection, IntersectionDistance, IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);
 const auto hit = RaycastSolidBlocks(blockWorld_, position, front);
 hasIntersectionBlock_ = hit.has_value();
 if (hit) {
  intersectionBlockPos_ = hit->blockPos;
 } else {
  intersectionBlockPos_ = glm::ivec3(0);
 }
 meshInstancesReady_ = false;
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
 // Clear existing index
 if (spatialIndex) {
     spatialIndex->Clear();
 }
 
 // Create new Octree with appropriate size
float worldSize = 1000.0f; // World size
   spatialIndex = std::make_unique<OctreeNode>(glm::vec3(0, 0, 0), worldSize);
 
 // Add all objects to index
 for (const auto& object : Objects) {
     spatialIndex->Insert(object);
 }
 
 spatialIndexDirty = false;
}

void World::InvalidateBlockMesh()
{
 meshCache_.MarkAllDirty();
 meshInstancesReady_ = false;
}

const std::vector<BlockInstance>& World::GetBlockRenderInstances()
{
 if (!meshInstancesReady_) {
  if (blockRegistry_) {
   meshCache_.RebuildDirtyChunks(blockWorld_, *blockRegistry_, 64);
  }
  meshInstancesReady_ = true;
 }
 return meshCache_.GetInstances();
}

void World::MarkBlockChunkDirty(glm::ivec3 blockPos)
{
 meshCache_.MarkDirty(ChunkManager::WorldToChunk(blockPos));
 meshInstancesReady_ = false;
}

void World::LoadBlocks(const std::string& file_name)
{
 if (!blockRegistry_) {
  return;
 }
 std::ifstream file(file_name);
 if (!file.is_open()) {
  return;
 }
 try {
  json data = json::parse(file);
  const json& blocks = data.at("blocks");
  for (const auto& entry : blocks) {
   const int x = entry.at("x").get<int>();
   const int y = entry.at("y").get<int>();
   const int z = entry.at("z").get<int>();
   const std::string type = entry.at("type").get<std::string>();
   const BlockId id = blockRegistry_->GetIdByTypeName(type);
   if (id != BLOCK_AIR) {
    blockWorld_.SetBlock(glm::ivec3(x, y, z), id);
   }
  }
 } catch (const json::exception& e) {
  std::cerr << "JSON parsing error in LoadBlocks: " << e.what() << std::endl;
 }
}

void World::SaveBlocks(const std::string& file_name)
{
 if (!blockRegistry_) {
  return;
 }
 json data;
 data["format_version"] = 1;
 json blocks = json::array();
 blockWorld_.ForEachBlock([&](glm::ivec3 pos, BlockId id) {
  const std::string& type = blockRegistry_->GetTypeNameById(id);
  if (type.empty()) {
   return;
  }
  blocks.push_back({
      {"x", pos.x},
      {"y", pos.y},
      {"z", pos.z},
      {"type", type},
  });
 });
 data["blocks"] = blocks;
 std::ofstream file(file_name);
 if (file.is_open()) {
  file << data.dump(4);
 }
}

void World::LoadChunks(const std::string& file_name)
{
 if (!blockRegistry_) {
  return;
 }
 std::ifstream file(file_name);
 if (!file.is_open()) {
  return;
 }
 try {
  json data = json::parse(file);
  for (const auto& chunkEntry : data.at("chunks")) {
   const int cx = chunkEntry.at("cx").get<int>();
   const int cy = chunkEntry.at("cy").get<int>();
   const int cz = chunkEntry.at("cz").get<int>();
   const glm::ivec3 chunkCoord(cx, cy, cz);
   for (const auto& voxel : chunkEntry.at("voxels")) {
    const int lx = voxel.at("lx").get<int>();
    const int ly = voxel.at("ly").get<int>();
    const int lz = voxel.at("lz").get<int>();
    const std::string type = voxel.at("type").get<std::string>();
    const BlockId id = blockRegistry_->GetIdByTypeName(type);
    if (id == BLOCK_AIR) {
     continue;
    }
    const glm::ivec3 worldPos(
        cx * CHUNK_SIZE + lx,
        cy * CHUNK_SIZE + ly,
        cz * CHUNK_SIZE + lz);
    blockWorld_.SetBlock(worldPos, id);
   }
  }
 } catch (const json::exception& e) {
  std::cerr << "JSON parsing error in LoadChunks: " << e.what() << std::endl;
 }
}

void World::SaveChunks(const std::string& file_name)
{
 if (!blockRegistry_) {
  return;
 }
 json data;
 data["format_version"] = 2;
 data["chunk_size"] = CHUNK_SIZE;
 json chunks = json::array();

 std::map<std::string, json> chunkMap;
 blockWorld_.ForEachBlock([&](glm::ivec3 worldPos, BlockId id) {
  const std::string& type = blockRegistry_->GetTypeNameById(id);
  if (type.empty()) {
   return;
  }
  const glm::ivec3 chunkCoord = ChunkManager::WorldToChunk(worldPos);
  const glm::ivec3 local = ChunkManager::WorldToLocal(worldPos);
  const std::string key = std::to_string(chunkCoord.x) + "," + std::to_string(chunkCoord.y) + "," + std::to_string(chunkCoord.z);
  if (chunkMap.find(key) == chunkMap.end()) {
   chunkMap[key] = json::object({
       {"cx", chunkCoord.x},
       {"cy", chunkCoord.y},
       {"cz", chunkCoord.z},
       {"voxels", json::array()},
   });
  }
  chunkMap[key]["voxels"].push_back({
      {"lx", local.x},
      {"ly", local.y},
      {"lz", local.z},
      {"type", type},
  });
 });

 for (auto& entry : chunkMap) {
  chunks.push_back(std::move(entry.second));
 }
 data["chunks"] = chunks;

 std::ofstream file(file_name);
 if (file.is_open()) {
  file << data.dump(4);
 }
}

void World::MigrateObjectsFromJson(const std::string& file_name)
{
 std::ifstream file(file_name);
 if (!file.is_open()) {
  return;
 }
 try {
  json objects = json::parse(file);
  for (const auto& object_data : objects) {
   const std::string type_name = object_data.value("type_name", "");
   if (type_name == "terrain_plane" || type_name.empty()) {
    continue;
   }
   const auto position_value = object_data.value("position", json::array());
   if (!position_value.is_array() || position_value.size() != 3) {
    continue;
   }
   const glm::ivec3 blockPos(
       static_cast<int>(std::round(position_value[0].get<float>())),
       static_cast<int>(std::round(position_value[1].get<float>())),
       static_cast<int>(std::round(position_value[2].get<float>())));
   const BlockId id = blockRegistry_->GetIdByTypeName(type_name);
   if (id != BLOCK_AIR) {
    blockWorld_.SetBlock(blockPos, id);
   }
  }
 } catch (const json::exception& e) {
  std::cerr << "JSON parsing error in MigrateObjectsFromJson: " << e.what() << std::endl;
 }
}

}
