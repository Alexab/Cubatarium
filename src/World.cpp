//#include <QPainter>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonValue>
//#include <QJsonArray>
//#include <QFile>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "World.h"
#include "Object.h"
#include "ObjectStorage.h"
#include "User.h"
#include "Prefab.h"
#include "PrefabUtil.h"
#include "ViewEngine.h"
#include "Camera.h"
#include "BlockRaycast.h"
#include "GridMath.h"
#include "ProceduralConfigIO.h"
#include "ProceduralSettings.h"
#include "worldgen/IWorldGenPipeline.h"
#include "worldgen/WorldGenContext.h"
#include "worldgen/PrefabFeaturePlacer.h"
#include "ChunkManager.h"
#include "CreatureBounds.h"
#include "CreaturePartMeshData.h"
#include "PlayerCapsule.h"
#include "Cube.h"
#include "Chunk.h"
#include "Prefab.h"
#include "ChunkStreamer.h"
#include "Player.h"
#include "Creature.h"
#include "CreatureInventory.h"
#include "CreatureDefinitionStorage.h"
#include "CreatureVisualFactory.h"
#include "Frustum.h"
#include "RenderSettings.h"
#include <map>
#include <unordered_set>

using json = nlohmann::json;

namespace cutum {

namespace {

constexpr float kMaxReasonablePlayerY = 512.0f;

constexpr float kMinReasonablePlayerY = -32.0f;

bool HasChunkJsonFiles(const std::string& chunks_dir)
{
 if (!std::filesystem::exists(chunks_dir) || !std::filesystem::is_directory(chunks_dir)) {
  return false;
 }
 for (const auto& entry : std::filesystem::directory_iterator(chunks_dir)) {
  if (entry.path().extension() == ".json") {
   return true;
  }
 }
 return false;
}

} // namespace

bool World::HasPersistedTerrainOnDisk(const std::string& world_folder_path)
{
 const std::string blocks_file = world_folder_path + "/blocks.json";
 if (std::filesystem::exists(blocks_file) && std::filesystem::file_size(blocks_file) > 2) {
  return true;
 }

 const std::string chunks_dir = world_folder_path + "/chunks";
 if (HasChunkJsonFiles(chunks_dir)) {
  return true;
 }

 const std::string chunks_file = world_folder_path + "/chunks.json";
 if (!std::filesystem::exists(chunks_file)) {
  return false;
 }

 try {
  std::ifstream file(chunks_file);
  if (!file.is_open()) {
   return false;
  }
  const json data = json::parse(file);
  if (data.value("storage", "") == "per_file") {
   return false;
  }
  return data.contains("chunks") && data["chunks"].is_array() && !data["chunks"].empty();
 } catch (const json::exception&) {
  return false;
 }
}

World::World(std::shared_ptr<ObjectStorage> object_storage, std::shared_ptr<ViewEngine> views)
 : ObjectStorageInstance(object_storage)
 , ViewInstance(views)
{
 if (ObjectStorageInstance && ObjectStorageInstance->GetTextureCubeStorage()) {
  blockRegistry_ = std::make_unique<BlockRegistry>(ObjectStorageInstance->GetTextureCubeStorage(),
                                                   blockDefinitions_);
 }
 IsIntersectionExists = false;
 hasIntersectionBlock_ = false;
}

void World::GenerateUsers()
{
 AddUser("Username");
 ApplySpawnToCamera();
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

void World::SetTerrainParams(uint32_t seed, const std::string& terrainType)
{
 worldSeed_ = seed;
 terrainType_ = terrainType;
 ProceduralSettings settings;
 settings.seed = seed;
 settings.generator = ProceduralGeneratorFromString(terrainType);
 ResolveProceduralDefaults(settings);
 ApplyGeneratorTierDefaults(settings);
 SetProceduralSettings(settings);
 if (blockRegistry_ && !streamer_) {
  streamer_ = std::make_unique<ChunkStreamer>(blockWorld_, *blockRegistry_, worldSeed_, 0, 8);
 }
}

void World::SetProceduralSettings(const ProceduralSettings& settings)
{
 proceduralSettings_ = settings;
 worldSeed_ = settings.seed;
 terrainType_ = ProceduralGeneratorToString(settings.generator);
 if (blockRegistry_ && !streamer_) {
  streamer_ = std::make_unique<ChunkStreamer>(blockWorld_, *blockRegistry_, worldSeed_, 0, 8);
 }
 RebuildWorldGenPipeline();
}

void World::RebuildWorldGenPipeline()
{
 if (!blockRegistry_) {
  worldGen_.reset();
  return;
 }
 WorldGenContext ctx{
     blockWorld_,
     *blockRegistry_,
     proceduralSettings_,
     prefabLibrary_,
     &meshCache_};
 worldGen_ = ProceduralWorldGenFactory::Create(ctx);
}

void World::SetRenderDistanceChunks(int distance)
{
 renderDistanceChunks_ = distance;
 if (streamer_) {
  streamer_->SetRenderDistance(distance);
 }
 meshCache_.SetRenderDistanceChunks(distance);
}

void World::InitStreamerCallbacks()
{
 if (!streamer_ || !blockRegistry_) {
  return;
 }
 streamer_->SetRenderDistance(renderDistanceChunks_);
 streamer_->SetEnabled(streamingEnabled_);
 streamer_->SetWorldFolder(worldFolderPath_);
 streamer_->SetCallbacks(
     [this](glm::ivec3 coord) {
      return LoadChunkFromFile(coord, worldFolderPath_) >= 0;
     },
     [this](glm::ivec3 coord) { SaveChunkToFile(coord, worldFolderPath_); },
     [this](glm::ivec3 coord) { meshCache_.MarkDirty(coord); },
     [this](int x, int z) {
      if (!allowProceduralFill_ || !worldGen_) {
       return;
      }
      worldGen_->GenerateColumn(x, z);
     },
     [this](glm::ivec3 coord) { meshCache_.RemoveChunk(coord); });
}

void World::GenerateWorldBlocks()
{
 if (!blockRegistry_) {
  return;
 }
 if (hasPersistedSave_ || loadedFromChunkSave_) {
  std::cerr << "GenerateWorldBlocks: skipped (persisted world on disk)" << std::endl;
  return;
 }
 if (!worldGen_) {
  RebuildWorldGenPipeline();
 }
 if (!worldGen_) {
  return;
 }

 const int patchRadiusBlocks = std::max(1, renderDistanceChunks_) * CHUNK_SIZE;
 if (streamingEnabled_) {
  worldGen_->GenerateSpawnPatch(0, 0, patchRadiusBlocks);
 } else {
  worldGen_->GenerateFullPatch(0, 0, patchRadiusBlocks);
 }
 SpawnPoint = worldGen_->DefaultSpawnPosition(0, 0);

 if (proceduralSettings_.fillFire && prefabLibrary_ && blockRegistry_) {
  WorldGenContext ctx{blockWorld_, *blockRegistry_, proceduralSettings_, prefabLibrary_,
                      &meshCache_};
  ctx.ResolveBlockIds();
  const int surfaceY = worldGen_->SurfaceYAt(8, 8);
  PlacePrefabAt(ctx, "fire_patch", glm::ivec3(8, surfaceY + 1, 8));
 }
}

void World::SetBlockDefinitionStorage(std::shared_ptr<BlockDefinitionStorage> definitions)
{
 blockDefinitions_ = std::move(definitions);
 if (blockRegistry_) {
  blockRegistry_->SetDefinitions(blockDefinitions_);
 } else if (ObjectStorageInstance && ObjectStorageInstance->GetTextureCubeStorage()) {
  blockRegistry_ = std::make_unique<BlockRegistry>(ObjectStorageInstance->GetTextureCubeStorage(),
                                                   blockDefinitions_);
 }
}

void World::RefreshBlockRegistry()
{
 if (blockRegistry_) {
  blockRegistry_->Reload();
 }
}

void World::RebuildBlockMesh()
{
 if (!blockRegistry_) {
  return;
 }
 meshCache_.RebuildAll(blockWorld_, *blockRegistry_);
 cachedBlockCount_ = blockWorld_.CountNonAir();
 blockWorldReady_ = cachedBlockCount_ > 0;
}

bool World::IsReasonablePlayerPosition(const glm::vec3& position) const
{
 if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
  return false;
 }
 if (position.y <= kMinReasonablePlayerY || position.y > kMaxReasonablePlayerY) {
  return false;
 }
 if (std::abs(position.x) > 100000.0f || std::abs(position.z) > 100000.0f) {
  return false;
 }
 return true;
}

void World::SanitizeUserPosition(const std::shared_ptr<User>& user)
{
 if (!user) {
  return;
 }
 if (!IsReasonablePlayerPosition(user->GetPosition())) {
  user->SetPosition(SpawnPoint);
  user->SetCameraOrientation(-90.0f, 0.0f);
 }
 if (std::abs(user->GetCameraPitch()) > 89.0f) {
  user->SetCameraOrientation(user->GetCameraYaw(), 0.0f);
 }
}

void World::ApplySpawnToCamera()
{
 glm::vec3 spawn = SpawnPoint;
 const PlayerCapsule cap = PlayerCapsule::Standing();
 while (CheckCollision(spawn, cap)) {
  spawn.y += 0.1f;
 }
 SpawnPoint = spawn;

 if (auto user = GetCurrentUser()) {
  user->SetPosition(SpawnPoint);
  user->SetCameraOrientation(-90.0f, 0.0f);
 }
 if (auto camera = GetCurrentUserCamera()) {
  camera->SetPosition(SpawnPoint);
  camera->SetOrientation(-90.0f, 0.0f);
  return;
 }
 if (ViewInstance && ViewInstance->GetActiveCamera()) {
  ViewInstance->GetActiveCamera()->SetPosition(SpawnPoint);
  ViewInstance->GetActiveCamera()->SetOrientation(-90.0f, 0.0f);
 }
}

std::optional<int> World::FindHighestSolidY(int x, int z) const
{
 if (!blockRegistry_) {
  return std::nullopt;
 }
 for (int y = 255; y >= 0; --y) {
  if (blockRegistry_->IsSolid(blockWorld_.GetBlock(glm::ivec3(x, y, z)))) {
   return y;
  }
 }
 return std::nullopt;
}

std::optional<float> World::QueryGroundFeetY(int worldX, int worldZ) const
{
 if (const std::optional<int> topY = FindHighestSolidY(worldX, worldZ)) {
  return BlockTopY(*topY);
 }
 return std::nullopt;
}

void World::EnsurePlayerOnGround()
{
 auto user = GetCurrentUser();
 if (!user || !blockRegistry_) {
  return;
 }

 std::string userName = CurrentUserName;
 for (const auto& entry : Users) {
  if (entry.second == user) {
   userName = entry.first;
   break;
  }
 }
 auto camera = GetUserCamera(userName);
 if (!camera) {
  return;
 }

 const PlayerCapsule cap = PlayerCapsule::Standing();
 glm::vec3 pos = user->GetPosition();
 const glm::ivec3 column = WorldPosToBlock(pos);
 int x = column.x;
 int z = column.z;

 std::optional<int> topY = FindHighestSolidY(x, z);
 if (!topY) {
  const glm::ivec3 spawnColumn = WorldPosToBlock(SpawnPoint);
  x = spawnColumn.x;
  z = spawnColumn.z;
  topY = FindHighestSolidY(x, z);
 }
 if (!topY) {
  ApplySpawnToCamera();
  if (auto cam = GetUserCamera(userName)) {
   cam->ResetVerticalPhysics();
  }
  return;
 }

 pos = BlockCenter(glm::ivec3(x, *topY, z));
 pos.y = BlockTopY(*topY) + cap.eyeHeight;
 while (CheckCollision(pos, cap)) {
  pos.y += 0.1f;
 }

 user->SetPosition(pos);
 camera->SetPosition(pos);
 camera->ResetVerticalPhysics();
}

void World::FinalizePlayerAfterWorldLoad()
{
 blockWorldReady_ = cachedBlockCount_ > 0;
 physicsSuspendFrames_ = 3;

 if (auto user = GetCurrentUser()) {
  SanitizeUserPosition(user);
  if (blockWorldReady_) {
   EnsurePlayerOnGround();
  } else {
   ApplySpawnToCamera();
  }
 } else {
  ApplySpawnToCamera();
 }

 if (auto camera = GetCurrentUserCamera()) {
  camera->ResetVerticalPhysics();
 }
}

void World::ApplyUserToCamera(const std::shared_ptr<User>& user)
{
 if (!user) {
  return;
 }
 SanitizeUserPosition(user);

 std::string userName = CurrentUserName;
 for (const auto& entry : Users) {
  if (entry.second == user) {
   userName = entry.first;
   break;
  }
 }
 if (auto camera = GetUserCamera(userName)) {
  camera->SetPosition(user->GetPosition());
  camera->SetOrientation(user->GetCameraYaw(), user->GetCameraPitch());
 }
}

void World::Create(const std::string& world_name)
{
 blockWorldReady_ = false;
 hasPersistedSave_ = false;
 loadedFromChunkSave_ = false;
 allowProceduralFill_ = true;
 RefreshBlockRegistry();
 blockWorld_.Clear();
 modifiedChunks_.clear();
 GenerateWorldBlocks();
 WorldName = world_name;
 allowProceduralFill_ = streamingEnabled_;
 InitStreamerCallbacks();
 RebuildBlockMesh();
 FinalizePlayerAfterWorldLoad();
}

void World::Load(const std::string& world_folder_path)
{
 worldFolderPath_ = world_folder_path;
 blockWorldReady_ = false;
 loadedFromChunkSave_ = false;
 blockWorld_.Clear();
 modifiedChunks_.clear();

 const std::string users_file_name = worldFolderPath_ + "/users.json";
 const std::string world_data_file_name = worldFolderPath_ + "/world_data.json";
 const std::string chunks_file_name = worldFolderPath_ + "/chunks.json";
 const std::string blocks_file_name = worldFolderPath_ + "/blocks.json";
 const std::string objects_file_name = worldFolderPath_ + "/objects.json";
 const std::string chunks_dir = worldFolderPath_ + "/chunks";

 hasPersistedSave_ = HasPersistedTerrainOnDisk(worldFolderPath_);
 allowProceduralFill_ = !hasPersistedSave_;

 LoadWorldData(world_data_file_name);
 creatures_.clear();
 nextCreatureId_ = 1;
 playerCreatureId_ = 0;
 controlledCreatureId_ = 0;

 LoadUsers(users_file_name);
 LoadCreatures(worldFolderPath_ + "/creatures.json");
 LinkUsersToPlayerCreatures();

 RefreshBlockRegistry();

 int chunkFilesRead = 0;
 size_t voxelsFromChunkFiles = 0;
 if (std::filesystem::exists(chunks_dir)) {
  for (const auto& entry : std::filesystem::directory_iterator(chunks_dir)) {
   if (entry.path().extension() != ".json") {
    continue;
   }
   const std::string stem = entry.path().stem().string();
   const size_t u1 = stem.find('_');
   const size_t u2 = stem.find('_', u1 + 1);
   if (u1 == std::string::npos || u2 == std::string::npos) {
    continue;
   }
   try {
    const int cx = std::stoi(stem.substr(0, u1));
    const int cy = std::stoi(stem.substr(u1 + 1, u2 - u1 - 1));
    const int cz = std::stoi(stem.substr(u2 + 1));
    const int placed = LoadChunkFromFile(glm::ivec3(cx, cy, cz), worldFolderPath_);
    if (placed >= 0) {
     ++chunkFilesRead;
     voxelsFromChunkFiles += static_cast<size_t>(placed);
    }
   } catch (const std::exception& e) {
    std::cerr << "Skipping chunk file " << entry.path().string() << ": " << e.what() << std::endl;
   }
  }
 } else if (std::filesystem::exists(chunks_file_name)) {
  LoadChunks(chunks_file_name);
  MigrateMonolithicChunksJson(chunks_file_name, worldFolderPath_);
  chunkFilesRead = 1;
  voxelsFromChunkFiles = blockWorld_.CountNonAir();
 }

 if (!hasPersistedSave_ && blockWorld_.CountNonAir() == 0 && std::filesystem::exists(blocks_file_name)) {
  LoadBlocks(blocks_file_name);
 }
 if (!hasPersistedSave_ && blockWorld_.CountNonAir() == 0 && std::filesystem::exists(objects_file_name)) {
  MigrateObjectsFromJson(objects_file_name);
 }

 const size_t blocksInWorld = blockWorld_.CountNonAir();
 loadedFromChunkSave_ = hasPersistedSave_ || chunkFilesRead > 0 || voxelsFromChunkFiles > 0
     || blocksInWorld > 0;
 allowProceduralFill_ = streamingEnabled_;

 std::cout << "World::Load: folder=" << worldFolderPath_ << std::endl;
 std::cout << "World::Load: chunk files=" << chunkFilesRead
           << " voxels from chunks=" << voxelsFromChunkFiles
           << " blocks in world=" << blocksInWorld
           << " loadedFromChunkSave=" << (loadedFromChunkSave_ ? "yes" : "no") << std::endl;

 if (blocksInWorld == 0 && !loadedFromChunkSave_ && blockRegistry_) {
  std::cout << "World::Load: empty world folder, generating procedural terrain." << std::endl;
  GenerateWorldBlocks();
 } else if (blocksInWorld == 0 && loadedFromChunkSave_) {
  std::cerr << "World::Load: chunk save on disk but 0 blocks loaded — no procedural fill. "
            << "Check models/blocks match chunk type names." << std::endl;
 }

 InitStreamerCallbacks();

 RebuildBlockMesh();
 FinalizePlayerAfterWorldLoad();
}

void World::Save(const std::string& world_folder_path)
{
 RefreshBlockRegistry();
 std::filesystem::create_directories(world_folder_path);
 worldFolderPath_ = world_folder_path;
 const std::string users_file_name = world_folder_path + "/users.json";
 const std::string world_data_file_name = world_folder_path + "/world_data.json";
 const std::string chunks_file_name = world_folder_path + "/chunks.json";
 const std::string chunks_dir = world_folder_path + "/chunks";

 std::filesystem::create_directories(chunks_dir);
 blockWorld_.GetChunkManager().ForEachChunk([&](const Chunk& chunk) {
  SaveChunkToFile(chunk.GetCoord(), world_folder_path);
 });

 json marker;
 marker["format_version"] = 3;
 marker["storage"] = "per_file";
 std::ofstream markerFile(chunks_file_name);
 if (markerFile.is_open()) {
  markerFile << marker.dump(4);
 }

 SaveUsers(users_file_name);
 SaveCreatures(world_folder_path + "/creatures.json");
 SaveWorldData(world_data_file_name);
 modifiedChunks_.clear();
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
 if (blockWorld_.GetBlock(blockPos) != id) {
  return false;
 }
 ++cachedBlockCount_;
 blockWorldReady_ = true;
 MarkBlockChunkDirty(blockPos);
 return true;
}

bool World::PlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos)
{
 if (!prefabLibrary_ || !blockRegistry_) {
  return false;
 }
 const Prefab* prefab = prefabLibrary_->Get(prefab_name);
 if (!prefab) {
  return false;
 }

 const PrefabPlacementStats stats = PlacePrefabAt(blockWorld_, *prefab, anchorWorldPos, true);
 if (stats.placedCount == 0) {
  return false;
 }
 for (const auto& voxel : prefab->voxels) {
  const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab->anchor;
  if (blockWorld_.GetBlock(worldPos) == voxel.id) {
   MarkBlockChunkDirty(worldPos);
  }
 }
 return true;
}

bool World::CanPlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos) const
{
 if (!prefabLibrary_ || !blockRegistry_) {
  return false;
 }
 const Prefab* prefab = prefabLibrary_->Get(prefab_name);
 if (!prefab) {
  return false;
 }
 return CanPlacePrefabAt(blockWorld_, *prefab, anchorWorldPos);
}

std::optional<glm::ivec3> World::FindPrefabAnchorFromView(const glm::vec3& position, const glm::vec3& front) const
{
 const auto hit = RaycastSolidBlocks(blockWorld_, *blockRegistry_, position, front);
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
 return hit->blockPos + normal;
}

bool World::AddUser(const std::string &name)
{
 if(Users.find(name) != Users.end())
  return false;

 if(name.empty())
  return false;

 Users[name] = std::make_shared<User>();
 auto user = Users[name];
 const glm::vec3 eyeOffset(0.0f, 1.62f, 0.0f);
 const glm::vec3 bodyOrigin = BodyOriginFromEye(SpawnPoint, eyeOffset);
 std::string speciesId = "human";
 if (creatureDefinitions_) {
  const std::string controlled = creatureDefinitions_->GetControlledDefaultSpeciesId();
  if (!controlled.empty()) {
   speciesId = controlled;
  }
 }
 const CreatureId pid = SpawnCreature(speciesId, bodyOrigin);
 user->SetPlayerCreatureId(pid);
 if (Player* player = dynamic_cast<Player*>(GetCreature(pid))) {
  player->BindUser(user);
  if (!user->GetSelectedSkinId().empty()) {
   player->SetSkinId(user->GetSelectedSkinId());
   if (const CreatureDefinition* def = GetCreatureDefinition(speciesId)) {
    player->SetVisual(CreateCreatureVisual(*def));
   }
  }
  CreatureInventory& inv = player->GetInventory();
  inv.InitCreativeDefaults();
  inv.EnsureDefaultHotbar();
 }
 if (Users.size() == 1) {
  playerCreatureId_ = pid;
  controlledCreatureId_ = pid;
 }
 auto camera = std::make_shared<Camera>(SpawnPoint, glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
 camera->SetFreeMove(false);
 const size_t viewId = ViewInstance->AddCameraReturnId(camera);
 user->SetViewId(viewId);
 if(Users.size() == 1) {
  SetCurrentUserName(name);
  ViewInstance->SetActiveCamera(viewId);
 }

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

std::shared_ptr<User> World::GetCurrentUser() const
{
 return const_cast<World*>(this)->GetUser(CurrentUserName);
}

CreatureInventory* World::GetPlayerInventory(const std::shared_ptr<User>& user)
{
 if (!user || user->GetPlayerCreatureId() == 0) {
  return nullptr;
 }
 if (Creature* creature = GetCreature(user->GetPlayerCreatureId())) {
  return &creature->GetInventory();
 }
 return nullptr;
}

const CreatureInventory* World::GetPlayerInventory(const std::shared_ptr<User>& user) const
{
 return const_cast<World*>(this)->GetPlayerInventory(user);
}

void World::EnsurePlayerHotbarCount(const std::shared_ptr<User>& user, size_t barCount)
{
 if (CreatureInventory* inv = GetPlayerInventory(user)) {
  inv->EnsureHotbarCount(barCount);
 }
}

bool World::SetCurrentUserName(const std::string& name)
{
 if(Users.find(name) == Users.end())
  return false;
 CurrentUserName = name;
 if (auto user = GetCurrentUser()) {
  if (user->GetPlayerCreatureId() != 0) {
   playerCreatureId_ = user->GetPlayerCreatureId();
   SetControlledCreature(playerCreatureId_);
  }
 }
 ApplyUserToCamera(GetCurrentUser());
 if (auto user = GetCurrentUser()) {
  ViewInstance->SetActiveCamera(user->GetViewId());
 }
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

bool World::AddObjectByView()
{
 return AddObjectByView(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
}

bool World::DelObjectByView()
{
 return DelObjectByView(GetCurrentUserCamera()->GetPosition(), GetCurrentUserCamera()->GetFront());
}

bool World::CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const
{
 distance_map.clear();
 const auto hit = RaycastSolidBlocks(blockWorld_, *blockRegistry_, position, front);
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

bool World::CheckPositionFree(const glm::vec3& position, float /*size*/) const
{
 return blockWorld_.IsAir(WorldPosToBlock(position));
}

std::optional<glm::vec3> World::FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const
{
 const auto hit = RaycastSolidBlocks(blockWorld_, *blockRegistry_, position, front);
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
 auto user = GetCurrentUser();
 if (!user) {
  return false;
 }

 Creature* controlled = GetControlledCreature();
 if (!controlled) {
  return false;
 }
 const std::string& blockType = controlled->GetInventory().GetActiveBlockTypeName();
 if (blockType.empty()) {
  return false;
 }

 auto object_pos = FindNearestFreeCubePosition(position, front);
 if (object_pos.has_value()) {
  if (AddObject(blockType, object_pos.value())) {
   UpdateIntersection(position, front);
   return true;
  }
 }
 return false;
}

bool World::PlaceActivePrefabByView()
{
 return PlaceActivePrefabByView(GetCurrentUserCamera()->GetPosition(),
                                GetCurrentUserCamera()->GetFront());
}

bool World::PlaceActivePrefabByView(const glm::vec3& position, const glm::vec3& front)
{
 auto user = GetCurrentUser();
 if (!user) {
  return false;
 }

 Creature* controlled = GetControlledCreature();
 if (!controlled) {
  return false;
 }
 const std::string& prefabName = controlled->GetInventory().GetActivePrefabName();
 if (prefabName.empty()) {
  return false;
 }

 const auto anchor = FindPrefabAnchorFromView(position, front);
 if (!anchor.has_value()) {
  return false;
 }
 if (PlacePrefab(prefabName, anchor.value())) {
  UpdateIntersection(position, front);
  return true;
 }
 return false;
}

bool World::DelObjectByView(const glm::vec3& position, const glm::vec3& front)
{
 const auto hit = RaycastSolidBlocks(blockWorld_, *blockRegistry_, position, front);
 if (!hit) {
  return false;
 }
 blockWorld_.SetBlock(hit->blockPos, BLOCK_AIR);
 if (cachedBlockCount_ > 0) {
  --cachedBlockCount_;
 }
 MarkBlockChunkDirty(hit->blockPos);
 UpdateIntersection(position, front);
 return true;
}


bool World::IsCameraInsideFluid(const glm::vec3& eye, BlockId* outFluid) const
{
 if (!blockRegistry_) {
  return false;
 }
 const glm::ivec3 cell = WorldPosToBlock(eye);
 const BlockId id = blockWorld_.GetBlock(cell);
 if (id == BLOCK_AIR || blockRegistry_->BlocksMovement(id)) {
  return false;
 }
 const glm::vec3 center = BlockCenter(cell);
 const glm::vec3 rel = eye - center;
 if (std::abs(rel.x) > 0.5f || std::abs(rel.y) > 0.5f || std::abs(rel.z) > 0.5f) {
  return false;
 }
 if (outFluid) {
  *outFluid = id;
 }
 return true;
}

World::SampledFluidState World::SampleFluidPhysicsVolume(const CollisionVolume& vol) const
{
 SampledFluidState state;
 if (!blockRegistry_) {
  return state;
 }
 std::unordered_map<BlockId, int> fluidWeights;
 const glm::vec3 center = vol.center;
 const glm::vec3 half = vol.halfExtents;
 const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
 const int radius = static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
 const glm::vec3 blockHalf(0.5f);
 for (int dx = -radius; dx <= radius; ++dx) {
  for (int dy = -radius; dy <= radius; ++dy) {
   for (int dz = -radius; dz <= radius; ++dz) {
    const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
    const BlockId id = blockWorld_.GetBlock(blockPos);
    if (id == BLOCK_AIR || blockRegistry_->BlocksMovement(id)) {
     continue;
    }
    const glm::vec3 blockCenter = BlockCenter(blockPos);
    if (!Cube::CheckAabbCollision(center, half, blockCenter, blockHalf)) {
     continue;
    }
    const auto& mov = blockRegistry_->Physics(id).movement;
    state.inFluid = true;
    fluidWeights[id] += 1;
    state.dragHorizontal = std::max(state.dragHorizontal, mov.dragHorizontal);
    state.sinkSpeed = std::max(state.sinkSpeed, mov.sinkSpeed);
    state.riseSpeed = std::max(state.riseSpeed, mov.riseSpeed);
   }
  }
 }
 if (state.inFluid) {
  state.blendWeight = 1.0f;
  int bestWeight = 0;
  for (const auto& entry : fluidWeights) {
   if (entry.second > bestWeight) {
    bestWeight = entry.second;
    state.dominantFluid = entry.first;
   }
  }
 }
 return state;
}

World::SampledFluidState World::SampleFluidPhysics(const glm::vec3& eyePos,
                                                   const PlayerCapsule& cap) const
{
 return SampleFluidPhysicsVolume(CollisionVolumeFromEye(eyePos, cap));
}

bool World::CheckBlockCollisionVolume(const CollisionVolume& vol) const
{
 if (!blockRegistry_) {
  return false;
 }
 const glm::vec3 center = vol.center;
 const glm::vec3 half = vol.halfExtents;
 const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
 const int radius = static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
 const glm::vec3 blockHalf(0.5f);
 for (int dx = -radius; dx <= radius; ++dx) {
  for (int dy = -radius; dy <= radius; ++dy) {
   for (int dz = -radius; dz <= radius; ++dz) {
    const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
    const BlockId id = blockWorld_.GetBlock(blockPos);
    if (!blockRegistry_->BlocksMovement(id)) {
     continue;
    }
    const glm::vec3 blockCenter = BlockCenter(blockPos);
    if (Cube::CheckAabbCollision(center, half, blockCenter, blockHalf)) {
     return true;
    }
   }
  }
 }
 return false;
}

bool World::CheckCreatureCollisionVolume(const CollisionVolume& vol,
                                         CreatureId skipCreatureId) const
{
 for (const auto& entry : creatures_) {
  if (entry.first == skipCreatureId) {
   continue;
  }
  const CollisionVolume other = entry.second->GetCollisionVolume();
  if (Cube::CheckAabbCollision(vol.center, vol.halfExtents, other.center,
                               other.halfExtents)) {
   return true;
  }
 }
 return false;
}

bool World::CheckCollisionVolume(const CollisionVolume& vol, CreatureId skipCreatureId) const
{
 if (CheckBlockCollisionVolume(vol)) {
  return true;
 }
 if (!entityCollisionEnabled_) {
  return false;
 }
 return CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool World::CheckCollision(const glm::vec3& eyePos, const PlayerCapsule& cap) const
{
 return CheckCollision(eyePos, cap, GetMovementCollisionSkipId());
}

bool World::CheckCollision(const glm::vec3& eyePos, const PlayerCapsule& cap,
                           CreatureId skipCreatureId) const
{
 return CheckCollisionVolume(CollisionVolumeFromEye(eyePos, cap), skipCreatureId);
}

bool World::HasGroundSupportVolume(const CollisionVolume& vol, float feetY) const
{
 if (!blockRegistry_) {
  return false;
 }
 const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
 const glm::ivec3 standCell(
     static_cast<int>(std::floor(vol.center.x)),
     supportY,
     static_cast<int>(std::floor(vol.center.z)));
 return blockRegistry_->BlocksMovement(blockWorld_.GetBlock(standCell));
}

bool World::HasGroundSupport(const glm::vec3& eyePos, const PlayerCapsule& cap) const
{
 return HasGroundSupportVolume(CollisionVolumeFromEye(eyePos, cap), cap.feetY(eyePos));
}

namespace {

constexpr float kCollisionMaxStep = 0.25f;
constexpr float kCollisionEpsilon = 0.01f;
constexpr int kCollisionMaxIterations = 64;

glm::vec3 ResolveMovementAxisEye(const World& world, const glm::vec3& fromEye, float axisDelta,
                                 int axis, const PlayerCapsule& cap, CreatureId skipCreatureId)
{
 if (std::abs(axisDelta) < 1e-8f) {
  return fromEye;
 }
 const float sign = axisDelta > 0.0f ? 1.0f : -1.0f;
 float remaining = std::abs(axisDelta);
 glm::vec3 pos = fromEye;
 glm::vec3 axisUnit(0.0f);
 axisUnit[axis] = 1.0f;

 int iterations = 0;
 while (remaining > 1e-6f && iterations < kCollisionMaxIterations) {
  const float step = std::min(remaining, kCollisionMaxStep);
  const glm::vec3 next = pos + axisUnit * step * sign;
  if (world.CheckCollisionVolume(CollisionVolumeFromEye(next, cap), skipCreatureId)) {
   glm::vec3 lo = pos;
   glm::vec3 hi = next;
   for (int i = 0; i < 8; ++i) {
    const glm::vec3 mid = (lo + hi) * 0.5f;
    if (world.CheckCollisionVolume(CollisionVolumeFromEye(mid, cap), skipCreatureId)) {
     hi = mid;
    } else {
     lo = mid;
    }
   }
   if (axis == 1 && sign < 0.0f) {
    pos = lo;
   } else {
    pos = lo - axisUnit * kCollisionEpsilon * sign;
   }
   break;
  }
  pos = next;
  remaining -= step;
  ++iterations;
 }
 return pos;
}

glm::vec3 ResolveMovementAxisBody(const World& world, const glm::vec3& fromBody, float axisDelta,
                                  int axis, const glm::vec3& currentSizeBlocks,
                                  CreatureId skipCreatureId)
{
 if (std::abs(axisDelta) < 1e-8f) {
  return fromBody;
 }
 const float sign = axisDelta > 0.0f ? 1.0f : -1.0f;
 float remaining = std::abs(axisDelta);
 glm::vec3 body = fromBody;
 glm::vec3 axisUnit(0.0f);
 axisUnit[axis] = 1.0f;

 int iterations = 0;
 while (remaining > 1e-6f && iterations < kCollisionMaxIterations) {
  const float step = std::min(remaining, kCollisionMaxStep);
  const glm::vec3 nextBody = body + axisUnit * step * sign;
  if (world.CheckCollisionVolume(CollisionVolumeFromBody(nextBody, currentSizeBlocks),
                                 skipCreatureId)) {
   glm::vec3 lo = body;
   glm::vec3 hi = nextBody;
   for (int i = 0; i < 8; ++i) {
    const glm::vec3 mid = (lo + hi) * 0.5f;
    if (world.CheckCollisionVolume(CollisionVolumeFromBody(mid, currentSizeBlocks),
                                   skipCreatureId)) {
     hi = mid;
    } else {
     lo = mid;
    }
   }
   if (axis == 1 && sign < 0.0f) {
    body = lo;
   } else {
    body = lo - axisUnit * kCollisionEpsilon * sign;
   }
   break;
  }
  body = nextBody;
  remaining -= step;
  ++iterations;
 }
 return body;
}

} // namespace

glm::vec3 World::ResolveMovementBody(const glm::vec3& bodyOrigin, const glm::vec3& delta,
                                     const glm::vec3& currentSizeBlocks,
                                     CreatureId skipCreatureId) const
{
 if (glm::dot(delta, delta) < 1e-10f) {
  return bodyOrigin;
 }
 glm::vec3 body = bodyOrigin;
 body = ResolveMovementAxisBody(*this, body, delta.y, 1, currentSizeBlocks, skipCreatureId);
 body = ResolveMovementAxisBody(*this, body, delta.x, 0, currentSizeBlocks, skipCreatureId);
 body = ResolveMovementAxisBody(*this, body, delta.z, 2, currentSizeBlocks, skipCreatureId);
 return body;
}

glm::vec3 World::ResolveMovement(const glm::vec3& eyePos, const glm::vec3& delta,
                                 const PlayerCapsule& cap, CreatureId skipCreatureId) const
{
 if (glm::dot(delta, delta) < 1e-10f) {
  return eyePos;
 }
 const glm::vec3 eyeOffset(0.0f, cap.eyeHeight, 0.0f);
 const glm::vec3 sizeBlocks(cap.halfWidth * 2.0f, cap.height, cap.halfWidth * 2.0f);
 const glm::vec3 body = BodyOriginFromEye(eyePos, eyeOffset);
 const glm::vec3 newBody = ResolveMovementBody(body, delta, sizeBlocks, skipCreatureId);
 return BoundsEyePosition(newBody, eyeOffset);
}

namespace {

glm::vec3 StepStandPosition(const glm::ivec3& stepCell, const PlayerCapsule& cap)
{
 const float feetY = BlockTopY(stepCell.y);
 return glm::vec3(static_cast<float>(stepCell.x), feetY + cap.eyeHeight,
                  static_cast<float>(stepCell.z));
}

bool FindSteppableLedge(const World& world, const BlockWorld& blockWorld,
                        const BlockRegistry& registry, const glm::vec3& eyePos,
                        const glm::vec3& dir, const PlayerCapsule& cap, glm::ivec3& outStepCell)
{
 const int dx = dir.x > 0.25f ? 1 : (dir.x < -0.25f ? -1 : 0);
 const int dz = dir.z > 0.25f ? 1 : (dir.z < -0.25f ? -1 : 0);
 if (dx == 0 && dz == 0) {
  return false;
 }
 const float feetY = cap.feetY(eyePos);
 const int supportY = static_cast<int>(std::floor(feetY - 0.04f));
 const glm::ivec3 standCell(
     static_cast<int>(std::floor(eyePos.x)),
     supportY,
     static_cast<int>(std::floor(eyePos.z)));
 if (!registry.BlocksMovement(blockWorld.GetBlock(standCell))) {
  return false;
 }
 const glm::ivec3 stepCell(standCell.x + dx, supportY + 1, standCell.z + dz);
 if (!registry.BlocksMovement(blockWorld.GetBlock(stepCell))) {
  return false;
 }
 const glm::vec3 landingEye = StepStandPosition(stepCell, cap);
 if (world.CheckCollision(landingEye, cap)) {
  return false;
 }
 outStepCell = stepCell;
 return true;
}

float DistanceToStepRiser(const glm::vec3& eyePos, const glm::ivec3& stepCell,
                          const glm::vec3& dir, const PlayerCapsule& cap)
{
 const glm::vec3 blockCenter(static_cast<float>(stepCell.x), static_cast<float>(stepCell.y),
                             static_cast<float>(stepCell.z));
 const glm::vec3 facePoint = blockCenter - glm::vec3(dir.x * 0.5f, 0.0f, dir.z * 0.5f);
 const glm::vec3 playerLead =
     eyePos + glm::vec3(dir.x * cap.halfWidth, 0.0f, dir.z * cap.halfWidth);
 return glm::dot(facePoint - playerLead, dir);
}

} // namespace

World::StepUpProbe World::ProbeStepUp(const glm::vec3& eyePos, const glm::vec3& horiz,
                                      const PlayerCapsule& cap,
                                      float maxTriggerDistance) const
{
 StepUpProbe probe{};
 if (!blockRegistry_) {
  return probe;
 }
 const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
 const float horizLen = glm::length(horizFlat);
 if (horizLen < 1e-6f) {
  return probe;
 }
 const glm::vec3 dir = horizFlat / horizLen;
 glm::ivec3 stepCell(0);
 if (!FindSteppableLedge(*this, blockWorld_, *blockRegistry_, eyePos, dir, cap, stepCell)) {
  return probe;
 }
 const float dist = DistanceToStepRiser(eyePos, stepCell, dir, cap);
 if (dist < -0.02f || dist > maxTriggerDistance) {
  return probe;
 }
 probe.valid = true;
 probe.distanceToLedge = dist;
 probe.moveDir = dir;
 probe.targetPos = StepStandPosition(stepCell, cap);
 return probe;
}

bool World::GetStepUpLanding(const glm::vec3& eyePos, const glm::vec3& horiz,
                             const PlayerCapsule& cap, float maxTriggerDistance,
                             glm::vec3& outLanding) const
{
 const StepUpProbe probe = ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
 if (!probe.valid) {
  return false;
 }
 glm::ivec3 stepCell(0);
 const glm::vec3 horizFlat(horiz.x, 0.0f, horiz.z);
 const float horizLen = glm::length(horizFlat);
 if (horizLen < 1e-6f) {
  return false;
 }
 const glm::vec3 dir = horizFlat / horizLen;
 if (!FindSteppableLedge(*this, blockWorld_, *blockRegistry_, eyePos, dir, cap, stepCell)) {
  return false;
 }
 const glm::ivec3 feetCell = WorldPosToBlock(
     glm::vec3(eyePos.x, cap.feetY(eyePos) + 0.01f, eyePos.z));
 if (feetCell.x == stepCell.x && feetCell.z == stepCell.z && feetCell.y >= stepCell.y) {
  return false;
 }

 outLanding = probe.targetPos
              - glm::vec3(probe.moveDir.x * 0.18f, 0.0f, probe.moveDir.z * 0.18f);
 if (!CheckCollision(outLanding, cap)) {
  return true;
 }

 glm::vec3 resolved = eyePos;
 const glm::vec3 stepDelta(probe.moveDir.x * 0.45f, 1.02f, probe.moveDir.z * 0.45f);
 resolved = ResolveMovement(eyePos, stepDelta, cap, GetMovementCollisionSkipId());
 const float movedH = glm::length(glm::vec2(resolved.x - eyePos.x, resolved.z - eyePos.z));
 if (movedH < 0.08f || CheckCollision(resolved, cap)) {
  return false;
 }
 outLanding = resolved;
 return true;
}

bool World::TryStepUp(glm::vec3& eyePos, const glm::vec3& horiz, const PlayerCapsule& cap,
                      float maxTriggerDistance) const
{
 glm::vec3 landing = eyePos;
 if (!GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance, landing)) {
  return false;
 }
 eyePos = landing;
 return true;
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
      const auto user_name = I.key();
      const auto user_data = I.value();

      AddUser(user_name);
      auto user = GetUser(user_name);
      if (!user) {
       continue;
      }

      glm::vec3 position = SpawnPoint;
      const auto position_value = user_data.value("position", json::array());
      if (position_value.is_array() && position_value.size() == 3) {
       position = glm::vec3(position_value[0].get<float>(),
                            position_value[1].get<float>(),
                            position_value[2].get<float>());
      }
      user->SetPosition(position);
      SanitizeUserPosition(user);

      if (user_data.contains("player_creature_id")) {
       const CreatureId savedId = user_data["player_creature_id"].get<CreatureId>();
       if (GetCreature(savedId)) {
        user->SetPlayerCreatureId(savedId);
        playerCreatureId_ = savedId;
       }
      }
      if (user_data.contains("selected_skin_id")) {
       user->SetSelectedSkinId(user_data["selected_skin_id"].get<std::string>());
      } else if (user_data.contains("selected_appearance_type")) {
       user->SetSelectedAppearanceTypeId(user_data["selected_appearance_type"].get<std::string>());
       user->SetSelectedSkinId(user_data["selected_appearance_type"].get<std::string>());
      }
      Creature* playerCreature = GetCreature(user->GetPlayerCreatureId());
      if (!playerCreature && playerCreatureId_ != 0) {
       user->SetPlayerCreatureId(playerCreatureId_);
       playerCreature = GetCreature(playerCreatureId_);
      }
      if (playerCreature) {
       const glm::vec3 eyeOffset = playerCreature->GetEyeOffset();
       playerCreature->SetBodyOrigin(BodyOriginFromEye(user->GetPosition(), eyeOffset));
      }

      float yaw = -90.0f;
      float pitch = 0.0f;
      if (user_data.contains("yaw")) {
       yaw = user_data["yaw"].get<float>();
      }
      if (user_data.contains("pitch")) {
       pitch = user_data["pitch"].get<float>();
      }
      user->SetCameraOrientation(yaw, pitch);

      const size_t hotbarCount = 2;
      if (playerCreature) {
       CreatureInventory& inv = playerCreature->GetInventory();
       inv.DeserializeFromJson(user_data, hotbarCount);
       if (inv.GetStorage().empty()) {
        inv.InitCreativeDefaults();
       }
       inv.EnsureDefaultHotbar();
       playerCreature->SetOrientation(ModelYawFromCameraYaw(yaw), pitch);
       if (!user->GetSelectedSkinId().empty()) {
        playerCreature->SetSkinId(user->GetSelectedSkinId());
        if (const CreatureDefinition* def =
                GetCreatureDefinition(playerCreature->GetTypeId())) {
         playerCreature->SetVisual(CreateCreatureVisual(*def));
        }
       }
      }

      if (auto camera = GetUserCamera(user_name)) {
       camera->SetPosition(position);
       camera->SetOrientation(yaw, pitch);
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
  const auto& user_name = I->first;
  auto user = I->second;

  glm::vec3 position = user->GetPosition();
  float yaw = user->GetCameraYaw();
  float pitch = user->GetCameraPitch();
  if (user_name == CurrentUserName) {
   if (auto camera = GetUserCamera(user_name)) {
    position = camera->GetPosition();
    yaw = camera->GetYaw();
    pitch = camera->GetPitch();
    user->SetPosition(position);
    user->SetCameraOrientation(yaw, pitch);
   }
  }

  json user_json;
  user_json["position"] = json::array({position.x, position.y, position.z});
  user_json["yaw"] = yaw;
  user_json["pitch"] = pitch;
  user_json["player_creature_id"] = user->GetPlayerCreatureId();
  if (!user->GetSelectedSkinId().empty()) {
   user_json["selected_skin_id"] = user->GetSelectedSkinId();
  } else if (!user->GetSelectedAppearanceTypeId().empty()) {
   user_json["selected_appearance_type"] = user->GetSelectedAppearanceTypeId();
  }

  if (Creature* playerCreature = GetCreature(user->GetPlayerCreatureId())) {
   playerCreature->GetInventory().SerializeToJson(user_json);
  }

  objects[user_name] = user_json;
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

     if (d.contains("terrain") && d["terrain"].is_string()) {
      terrainType_ = d["terrain"].get<std::string>();
     }
     if (d.contains("world_seed")) {
      worldSeed_ = d["world_seed"].get<uint32_t>();
     }
     if (d.contains("procedural") && d["procedural"].is_object()) {
      proceduralSettings_ = ParseProceduralSettings(d);
      terrainType_ = ProceduralGeneratorToString(proceduralSettings_.generator);
      worldSeed_ = proceduralSettings_.seed;
     } else {
      proceduralSettings_.seed = worldSeed_;
      proceduralSettings_.generator = ProceduralGeneratorFromString(terrainType_);
      ResolveProceduralDefaults(proceduralSettings_);
      ApplyGeneratorTierDefaults(proceduralSettings_);
     }
     RebuildWorldGenPipeline();
 } catch (const json::exception& e) {
     std::cerr << "JSON parsing error in LoadWorldData: " << e.what() << std::endl;
 }
}

void World::SaveWorldData(const std::string &file_name)
{
 json world_data;

 world_data["world_name"] = WorldName;
 world_data["terrain"] = terrainType_;
 world_data["world_seed"] = worldSeed_;
 WriteProceduralSettings(world_data, proceduralSettings_);

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
 if (!blockWorldReady_) {
  return;
 }
 if (physicsSuspendFrames_ > 0) {
  --physicsSuspendFrames_;
  return;
 }

 auto t_begin = std::chrono::high_resolution_clock::now();

 auto camera = GetCurrentUserCamera();
 Creature* controlled = GetControlledCreature();
 const float prevPlayerY = camera ? camera->GetPosition().y : 0.0f;
 const float dt = camera ? camera->GetDeltaTime() : 0.0f;

 if (camera && streamer_ && streamingEnabled_) {
  const glm::vec3 eyePos = camera->GetPosition();
  float feetY = FeetYFromEye(eyePos, controlled ? controlled->GetEyeOffset().y : 1.62f);
  if (controlled) {
   feetY = BoundsFeetY(controlled->GetBodyOrigin());
  }
  const glm::ivec3 feetBlock = WorldPosToBlock(glm::vec3(eyePos.x, feetY + 0.01f, eyePos.z));
  streamer_->EnsureCollisionChunks(feetBlock);
 }

 // TODO(CREATURE_AGENTS): creatureActivityDirector_.TickAgents(*this, dt);
 ForEachCreature([&](Creature& creature) {
  if (controlledCreatureId_ != 0 && creature.GetId() == controlledCreatureId_) {
   return;
  }
  creature.ApplyIntent(*this, dt);
 });

 bool is_moved = camera && camera->DoMovement(this);

 if (controlled && camera) {
  const glm::vec3 eye = camera->GetPosition();
  float feetY = FeetYFromEye(eye, controlled->GetEyeOffset().y);
  if (!camera->GetFreeMove() && camera->HasAnchoredFeet()) {
   const int gx = static_cast<int>(std::floor(eye.x));
   const int gz = static_cast<int>(std::floor(eye.z));
   if (const std::optional<float> gy = QueryGroundFeetY(gx, gz)) {
    feetY = *gy;
   }
  }
  controlled->SetBodyOrigin(glm::vec3(eye.x, feetY, eye.z));
  controlled->GetLocomotion().SetStanceBlendForView(camera->GetStanceBlend());
  controlled->GetLocomotion().SyncFeetAnchorFromView(feetY, camera->HasAnchoredFeet());
  controlled->SetOrientation(ModelYawFromCameraYaw(camera->GetYaw()), camera->GetPitch());
  controlled->SyncBoundsFromStance();
  controlled->GetLocomotion().SetMode(
      camera->GetFreeMove() ? CreatureMovementMode::Flying : CreatureMovementMode::Walking);
  is_moved = true;
 }

 if (camera) {
  UpdateStreaming();
  blockWorldReady_ = true;
 }

 if (is_moved && camera) {
  if (auto user = GetCurrentUser()) {
   user->SetPosition(camera->GetPosition());
   user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
  }
  UpdateIntersection(camera->GetPosition(), camera->GetFront());
 }

 auto t_end = std::chrono::high_resolution_clock::now();
 DurationDoMovementMks = std::chrono::duration<double, std::micro>(t_end-t_begin).count();
 UpdateMovementDiagnostics(camera, prevPlayerY);
}

void World::UpdateMovementDiagnostics(const std::shared_ptr<Camera>& camera, float prevPlayerY)
{
 movementDiagnostics_ = MovementDiagnostics{};
 if (!camera) {
  return;
 }

 const glm::vec3 playerPos = camera->GetPosition();
 const PlayerCapsule cap = camera->GetPlayerCapsule();
 movementDiagnostics_.feetBlock = WorldPosToBlock(
     glm::vec3(playerPos.x, cap.feetY(playerPos) + 0.01f, playerPos.z));
 movementDiagnostics_.feetChunk = ChunkManager::WorldToChunk(movementDiagnostics_.feetBlock);
 movementDiagnostics_.feetChunkLoaded = blockWorld_.GetChunkManager().HasChunk(movementDiagnostics_.feetChunk);
 movementDiagnostics_.feetIsAir = blockWorld_.IsAir(movementDiagnostics_.feetBlock);
 movementDiagnostics_.meshDrawCount = GetRenderInstanceCount();
 movementDiagnostics_.deltaTime = camera->GetDeltaTime();

 if (hasLastPlayerY_) {
  movementDiagnostics_.playerYDrop = prevPlayerY - playerPos.y;
 } else {
  movementDiagnostics_.playerYDrop = 0.0f;
 }
 hasLastPlayerY_ = true;
 lastPlayerY_ = playerPos.y;

 if (streamer_) {
  const auto& stats = streamer_->GetLastFrameStats();
  movementDiagnostics_.streamingLoads = stats.loadsThisFrame;
  movementDiagnostics_.streamingUnloads = stats.unloadsThisFrame;
  for (const glm::ivec3& coord : stats.unloadedCoords) {
   if (coord == movementDiagnostics_.feetChunk) {
    movementDiagnostics_.feetInUnloadList = true;
    break;
   }
  }
 }

 const double frameMs = (DurationDoMovementMks + (ViewInstance ? ViewInstance->GetDurationUpdateMks() : 0.0)) / 1000.0;
 movementDiagnostics_.hitchDetected = frameMs > 50.0 || movementDiagnostics_.deltaTime > 0.1f;
 movementDiagnostics_.fallThroughSuspected =
     movementDiagnostics_.playerYDrop > 2.0f &&
     (movementDiagnostics_.feetIsAir || !movementDiagnostics_.feetChunkLoaded) &&
     movementDiagnostics_.meshDrawCount > 0;

 if (movementDiagnostics_.hitchDetected || movementDiagnostics_.fallThroughSuspected ||
     movementDiagnostics_.playerYDrop > 2.0f) {
  std::cerr << "[movement-debug] cameraDt=" << movementDiagnostics_.deltaTime
            << " frameMs=" << frameMs
            << " yDrop=" << movementDiagnostics_.playerYDrop
            << " feetChunk=(" << movementDiagnostics_.feetChunk.x << ","
            << movementDiagnostics_.feetChunk.y << "," << movementDiagnostics_.feetChunk.z << ")"
            << " hasChunk=" << movementDiagnostics_.feetChunkLoaded
            << " feetAir=" << movementDiagnostics_.feetIsAir
            << " meshDraw=" << movementDiagnostics_.meshDrawCount
            << " loads=" << movementDiagnostics_.streamingLoads
            << " unloads=" << movementDiagnostics_.streamingUnloads
            << " feetUnloaded=" << movementDiagnostics_.feetInUnloadList
            << std::endl;
 }
}

void World::UpdateStreaming()
{
 if (!streamer_ || !streamingEnabled_) {
  return;
 }
 if (auto camera = GetCurrentUserCamera()) {
  const PlayerCapsule cap = camera->GetPlayerCapsule();
  streamer_->Update(
      WorldPosToBlock(camera->GetPosition()),
      camera->GetPosition(),
      cap);
 }
}

size_t World::GetRenderInstanceCount() const
{
 if (renderSettings_.greedyMeshing) {
  return meshCache_.GetGreedyVertexCount();
 }
 return meshCache_.GetInstanceCount();
}

void World::UpdateIntersection(const glm::vec3& position, const glm::vec3& front)
{
 IsIntersectionExists = CheckRayIntersection(position, front, Intersection, IntersectionDistance, IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);
 const auto hit = RaycastSolidBlocks(blockWorld_, *blockRegistry_, position, front);
 hasIntersectionBlock_ = hit.has_value();
 if (hit) {
  intersectionBlockPos_ = hit->blockPos;
 } else {
  intersectionBlockPos_ = glm::ivec3(0);
 }

 if (auto user = GetCurrentUser()) {
  if (auto camera = GetCurrentUserCamera()) {
   user->SetPosition(camera->GetPosition());
   user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
  }
 }
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

void World::InvalidateBlockMesh()
{
 if (blockRegistry_) {
  meshCache_.MarkAllDirtyFromWorld(blockWorld_);
 }
}

void World::SetRenderSettings(const RenderSettings& settings)
{
 renderSettings_ = settings;
 meshCache_.SetRenderSettings(settings);
}

const std::vector<FaceInstance>& World::GetBlockRenderInstances()
{
 if (blockRegistry_ && meshCache_.HasPendingDirty()) {
  const int rebuildBudget = renderSettings_.greedyMeshing ? 128 : 32;
  meshCache_.RebuildDirtyChunks(blockWorld_, *blockRegistry_, rebuildBudget);
  if (!meshCache_.HasPendingDirty()) {
   cachedBlockCount_ = blockWorld_.CountNonAir();
  }
 }
 if (auto camera = GetCurrentUserCamera()) {
  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  const glm::mat4 vp = proj * view;
  meshCache_.UpdateVisibleInstances(
      Frustum::FromViewProjection(vp), vp, camera->GetPosition());
 }
 return meshCache_.GetFaceInstances();
}

const std::vector<GreedyMeshBatch>& World::GetGreedyRenderBatches()
{
 GetBlockRenderInstances();
 return meshCache_.GetGreedyBatches();
}

size_t World::GetGreedyVertexCount() const
{
 return meshCache_.GetGreedyVertexCount();
}

uint64_t World::GetMeshRevision() const
{
 return meshCache_.GetMeshRevision();
}

uint64_t World::GetCullRevision() const
{
 return meshCache_.GetCullRevision();
}

void World::MarkBlockChunkDirty(glm::ivec3 blockPos)
{
 const glm::ivec3 chunkCoord = ChunkManager::WorldToChunk(blockPos);
 modifiedChunks_.insert(chunkCoord);

 if (blockRegistry_) {
  meshCache_.RebuildChunkImmediate(blockWorld_, *blockRegistry_, chunkCoord);
  for (const glm::ivec3& offset : NEIGHBOR_OFFSETS) {
   meshCache_.RebuildChunkImmediate(
       blockWorld_, *blockRegistry_, ChunkManager::WorldToChunk(blockPos + offset));
  }
 } else {
  meshCache_.MarkDirty(chunkCoord);
  for (const glm::ivec3& offset : NEIGHBOR_OFFSETS) {
   meshCache_.MarkDirty(ChunkManager::WorldToChunk(blockPos + offset));
  }
 }
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
  if (data.value("storage", "") == "per_file") {
   return;
  }
  if (!data.contains("chunks") || !data["chunks"].is_array()) {
   return;
  }
  for (const auto& chunkEntry : data["chunks"]) {
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

void World::SaveChunkToFile(glm::ivec3 chunkCoord, const std::string& world_folder)
{
 if (!blockRegistry_) {
  return;
 }
 const Chunk* chunk = blockWorld_.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  return;
 }

 json data;
 data["format_version"] = 2;
 data["cx"] = chunkCoord.x;
 data["cy"] = chunkCoord.y;
 data["cz"] = chunkCoord.z;
 json voxels = json::array();

 for (int z = 0; z < CHUNK_SIZE; ++z) {
  for (int y = 0; y < CHUNK_SIZE; ++y) {
   for (int x = 0; x < CHUNK_SIZE; ++x) {
    const glm::ivec3 local(x, y, z);
    const BlockId id = chunk->GetBlockLocal(local);
    if (id == BLOCK_AIR) {
     continue;
    }
    const std::string& type = blockRegistry_->GetTypeNameById(id);
    if (type.empty()) {
     continue;
    }
    voxels.push_back({
        {"lx", x},
        {"ly", y},
        {"lz", z},
        {"type", type},
    });
   }
  }
 }

 data["voxels"] = voxels;
 const std::string chunks_dir = world_folder + "/chunks";
 std::filesystem::create_directories(chunks_dir);
 const std::string file_name = chunks_dir + "/" +
     std::to_string(chunkCoord.x) + "_" +
     std::to_string(chunkCoord.y) + "_" +
     std::to_string(chunkCoord.z) + ".json";
 std::ofstream file(file_name);
 if (file.is_open()) {
  file << data.dump(4);
 }
}

int World::LoadChunkFromFile(glm::ivec3 chunkCoord, const std::string& world_folder)
{
 if (!blockRegistry_) {
  return -1;
 }
 const std::string file_name = world_folder + "/chunks/" +
     std::to_string(chunkCoord.x) + "_" +
     std::to_string(chunkCoord.y) + "_" +
     std::to_string(chunkCoord.z) + ".json";
 std::ifstream file(file_name);
 if (!file.is_open()) {
  return -1;
 }

 try {
  json data = json::parse(file);
  int placed = 0;
  for (const auto& voxel : data.at("voxels")) {
   const int lx = voxel.at("lx").get<int>();
   const int ly = voxel.at("ly").get<int>();
   const int lz = voxel.at("lz").get<int>();
   const std::string type = voxel.at("type").get<std::string>();
   const BlockId id = blockRegistry_->GetIdByTypeName(type);
   if (id == BLOCK_AIR) {
    continue;
   }
   const glm::ivec3 worldPos(
       chunkCoord.x * CHUNK_SIZE + lx,
       chunkCoord.y * CHUNK_SIZE + ly,
       chunkCoord.z * CHUNK_SIZE + lz);
   blockWorld_.SetBlock(worldPos, id);
   ++placed;
  }
  if (placed == 0) {
   return -1;
  }
  return placed;
 } catch (const json::exception& e) {
  std::cerr << "JSON parsing error in LoadChunkFromFile " << file_name << ": " << e.what()
            << std::endl;
  return -1;
 }
}

void World::MigrateMonolithicChunksJson(const std::string& /*chunks_file*/, const std::string& world_folder)
{
 blockWorld_.GetChunkManager().ForEachChunk([&](const Chunk& chunk) {
  SaveChunkToFile(chunk.GetCoord(), world_folder);
 });
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
