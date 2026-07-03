// #include <QPainter>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QJsonValue>
// #include <QJsonArray>
// #include <QFile>
#include "World/Core/World.h"
#include "Activity/WorldCreatureActivitySink.h"
#include "App/Settings/RenderSettings.h"
#include "Core/Progress/IUProgressSink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Primitives/Cube.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Collision/VoxelDdaTraversal.h"
#include "World/Core/WorldCooperativeOps.h"
#include "World/Diagnostics/MovementDiagnosticsRecorder.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/FluidCellState.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/PhysicsProfileFactory.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Physics/WorldChunkDirtyService.h"
#include "World/Physics/WorldMovementPhysicsService.h"
#include "World/Physics/WorldPhysicsScheduler.h"
#include "World/Raycast/BlockRaycast.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr float kMaxReasonablePlayerY = 512.0f;
constexpr float kMinReasonablePlayerY = -32.0f;

} // namespace

UWorld::UWorld(std::shared_ptr<UTextureCubeStorage> texture_cube,
               std::shared_ptr<UViewEngine> views)
    : TextureCubeInstance(texture_cube), ViewInstance(views),
      MeshService(std::make_unique<UWorldMeshService>()),
      Streaming(std::make_unique<UWorldStreaming>()),
      Persistence(std::make_unique<UWorldPersistence>()), Environment(*this),
      Collision(BlockWorld, &Environment)
{
  if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    Collision.SetBlockRegistry(BlockRegistry.get());
  }
  IsIntersectionExists = false;
  HasIntersectionBlock = false;
  Environment.Initialize();
  ConfigurePhysicsServices();
}

UWorld::~UWorld() = default;

bool UWorld::HasPersistedTerrainOnDisk(const std::string &world_folder_path)
{
  return UWorldPersistence::HasPersistedTerrainOnDisk(world_folder_path);
}

UChunkStorageService &UWorld::GetChunkStorage()
{
  return Persistence->GetChunkStorage();
}

const UChunkStorageService &UWorld::GetChunkStorage() const
{
  return Persistence->GetChunkStorage();
}

const std::string &UWorld::GetWorldFolderPath() const
{
  return Persistence->GetWorldFolderPath();
}

void UWorld::SetWorldFolderPath(const std::string &path)
{
  Persistence->SetWorldFolderPath(path);
}

void UWorld::GenerateUsers()
{
  AddUser("Username");
  ApplySpawnToCamera();
}

std::string UWorld::GetWorldName() const { return WorldName; }

void UWorld::SetWorldName(const std::string &value) { WorldName = value; }

glm::vec3 UWorld::GetSpawnPoint() const { return SpawnPoint; }

void UWorld::SetSpawnPoint(glm::vec3 value) { SpawnPoint = value; }

void UWorld::SetTerrainParams(uint32_t Seed, const std::string &terrainType)
{
  WorldSeed = Seed;
  TerrainType = terrainType;
  ProceduralSettings settings;
  settings.Seed = Seed;
  settings.Generator = ProceduralGeneratorFromString(terrainType);
  ResolveProceduralDefaults(settings);
  ApplyGeneratorTierDefaults(settings);
  SetProceduralSettings(settings);
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
  }
}

void UWorld::SetProceduralSettings(const ProceduralSettings &settings,
                                   bool rebuildPipeline)
{
  ProceduralTemplate = settings;
  WorldSeed = settings.Seed;
  TerrainType = ProceduralGeneratorToString(settings.Generator);
  MaxLoadOpsPerFrame = settings.MaxLoadOpsPerFrame;
  MaxUnloadOpsPerFrame = settings.MaxUnloadOpsPerFrame;
  if (BlockRegistry)
  {
    Streaming->EnsureStreamer(BlockWorld, *BlockRegistry, WorldSeed,
                              ProceduralTemplate);
  }
  if (rebuildPipeline)
  {
    RebuildWorldGenPipeline();
  }
}

void UWorld::SetWorldGenSets(WorldGenSets sets)
{
  WorldGenSetsData = std::move(sets);
  RebuildResolvedObjectFeatures();
  RebuildWorldGenPipeline();
}

void UWorld::SaveWorldGenSetsToDisk()
{
  if (GetWorldFolderPath().empty())
  {
    return;
  }
  Persistence->SaveWorldData(*this, GetWorldFolderPath() + "/world_data.json");
}

void UWorld::RebuildResolvedObjectFeatures()
{
  if (UObjectFeatureConfigStorage::IsLoaded())
  {
    ResolvedObjectFeatures = ResolveObjectFeatures(
        WorldGenSetsData, UObjectFeatureConfigStorage::Get());
  }
}

void UWorld::RebuildWorldGenPipeline()
{
  if (!BlockRegistry)
  {
    WorldGen.reset();
    return;
  }
  RebuildResolvedObjectFeatures();
  WorldGenContext ctx{BlockWorld, *BlockRegistry, ProceduralTemplate,
                      ObjectLibrary};
  ctx.WorldgenOwnerPackId = WorldgenOwnerPackId;
  ctx.ObjectFeatures = &ResolvedObjectFeatures;
  ctx.OnColumnMeshDirty = [this](int world_x, int world_z, int min_y, int max_y)
  { MarkColumnMeshDirty(world_x, world_z, min_y, max_y); };
  WorldGen = UProceduralWorldGenFactory::Create(ctx);
}

void UWorld::SetRenderDistanceChunks(int distance)
{
  RenderDistanceChunks = distance;
  EffectiveRenderDistance = distance;
  if (Streaming->HasStreamer())
  {
    Streaming->SetRenderDistance(distance);
  }
  MeshService->SetRenderDistanceChunks(distance);
}

void UWorld::SetChunkWriteFormat(ChunkWriteFormat format)
{
  Persistence->SetChunkWriteFormat(format);
}

ChunkWriteFormat UWorld::GetChunkWriteFormat() const
{
  return Persistence->GetChunkWriteFormat();
}

void UWorld::InitStreamerCallbacks()
{
  Streaming->InitStreamerCallbacks(*this);
}

void UWorld::TickAsyncChunkSystems()
{
  Streaming->TickAsyncChunkSystems(*this);
}

void UWorld::TickMeshEmerge()
{
  if (!BlockRegistry)
  {
    return;
  }
  Streaming->TickMeshEmerge(*this);
}

void UWorld::RefreshStreamerSettings()
{
  Streaming->RefreshStreamerSettings(ProceduralTemplate, MaxLoadOpsPerFrame,
                                     MaxUnloadOpsPerFrame);
}

void UWorld::SetStreamingEnabled(bool enabled)
{
  Streaming->SetStreamingEnabled(enabled);
}

bool UWorld::IsStreamingEnabled() const
{
  return Streaming->IsStreamingEnabled();
}

void UWorld::LoadInitialStreamingChunks()
{
  Persistence->LoadInitialTerrainColumns(*this, SpawnPoint,
                                         RenderDistanceChunks);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorld.GetChunkManager().ForEachChunk(
      [this](const UChunk &chunk)
      { MeshService->MarkDirty(chunk.GetCoord()); });
}

void UWorld::GenerateWorldBlocks()
{
  if (!BlockRegistry)
  {
    return;
  }
  if (HasPersistedSave || LoadedFromChunkSave)
  {
    std::cerr << "GenerateWorldBlocks: skipped (persisted world on disk)"
              << std::endl;
    return;
  }
  if (!WorldGen)
  {
    RebuildWorldGenPipeline();
  }
  if (!WorldGen)
  {
    return;
  }

  const int patchRadiusBlocks = std::max(1, RenderDistanceChunks) * CHUNK_SIZE;
  if (IsStreamingEnabled())
  {
    WorldGen->GenerateSpawnPatch(0, 0, patchRadiusBlocks);
  }
  else
  {
    WorldGen->GenerateFullPatch(0, 0, patchRadiusBlocks);
  }
  SpawnPoint = WorldGen->DefaultSpawnPosition(0, 0);

  RebuildBlockMesh();
}

void UWorld::SetBlockDefinitionStorage(
    std::shared_ptr<UBlockDefinitionStorage> definitions)
{
  BlockDefinitions = std::move(definitions);
  if (BlockRegistry)
  {
    BlockRegistry->SetDefinitions(BlockDefinitions);
  }
  else if (TextureCubeInstance)
  {
    BlockRegistry =
        std::make_unique<UBlockRegistry>(TextureCubeInstance, BlockDefinitions);
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
    }
    Collision.SetBlockRegistry(BlockRegistry.get());
  }
}

void UWorld::SetBlockMergeRegistry(
    std::shared_ptr<UBlockMergeRegistry> merge_registry)
{
  BlockMergeRegistry = std::move(merge_registry);
  if (BlockRegistry)
  {
    BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
  }
}

void UWorld::OnBlockRegistryChanged()
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  RebuildBlockMesh();
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnBlockRegistryRuntimeOverlayChanged()
{
  RefreshBlockRegistry();
  RebuildWorldGenPipeline();
  MeshService->MarkAllDirtyFromWorld(BlockWorld);
  if (OnBlockRegistryChangedCallback)
  {
    OnBlockRegistryChangedCallback();
  }
}

void UWorld::OnCreatureCatalogChanged()
{
  ReloadAllCreatureVisuals();
  if (OnCreatureCatalogChangedCallback)
  {
    OnCreatureCatalogChangedCallback();
  }
}

void UWorld::ReloadAllCreatureVisuals()
{
  Environment.ReloadAllCreatureVisuals();
}

void UWorld::WaitForPendingMeshJobs() { MeshService->WaitForAsyncMeshIdle(); }

void UWorld::RefreshBlockRegistry()
{
  WaitForPendingMeshJobs();
  if (BlockRegistry)
  {
    if (BlockMergeRegistry)
    {
      BlockRegistry->SetMergeRegistry(BlockMergeRegistry);
      if (BlockDefinitions)
      {
        BlockMergeRegistry->PopulateBlockDefinitionStorage(*BlockDefinitions);
      }
    }
    if (BlockDefinitions)
    {
      BlockRegistry->SetDefinitions(BlockDefinitions);
      BlockWorld.SetFluidDefinitions(BlockDefinitions.get());
    }
    BlockRegistry->Reload();
  }
}

void UWorld::RebuildBlockMesh()
{
  if (!BlockRegistry)
  {
    return;
  }
  MeshService->RebuildAll(BlockWorld, *BlockRegistry);
  CachedBlockCount = BlockWorld.CountNonAir();
  BlockWorldReady = CachedBlockCount > 0;
}

bool UWorld::IsReasonablePlayerPosition(const glm::vec3 &position) const
{
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z))
  {
    return false;
  }
  if (position.y <= kMinReasonablePlayerY || position.y > kMaxReasonablePlayerY)
  {
    return false;
  }
  if (std::abs(position.x) > 100000.0f || std::abs(position.z) > 100000.0f)
  {
    return false;
  }
  return true;
}

void UWorld::SanitizeUserPosition(const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  if (!IsReasonablePlayerPosition(user->GetPosition()))
  {
    user->SetPosition(SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (std::abs(user->GetCameraPitch()) > 89.0f)
  {
    user->SetCameraOrientation(user->GetCameraYaw(), 0.0f);
  }
}

void UWorld::ApplySpawnToCamera()
{
  glm::vec3 spawn = SpawnPoint;
  const PlayerCapsule cap = PlayerCapsule::Standing();
  DepenetrateEye(spawn, cap, GetMovementCollisionSkipId());
  SpawnPoint = spawn;

  if (auto user = GetCurrentUser())
  {
    user->SetPosition(SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (auto camera = GetCurrentUserCamera())
  {
    camera->SetPosition(SpawnPoint);
    camera->SetOrientation(-90.0f, 0.0f);
    return;
  }
  if (ViewInstance && ViewInstance->GetActiveCamera())
  {
    ViewInstance->GetActiveCamera()->SetPosition(SpawnPoint);
    ViewInstance->GetActiveCamera()->SetOrientation(-90.0f, 0.0f);
  }
}

std::optional<int> UWorld::FindHighestSolidY(int x, int z) const
{
  return Collision.FindHighestSolidY(x, z);
}

std::optional<float> UWorld::QueryGroundFeetYColumn(int worldX,
                                                    int worldZ) const
{
  return Collision.QueryGroundFeetYColumn(worldX, worldZ);
}

std::optional<float> UWorld::QueryGroundFeetYUnder(int worldX, int worldZ,
                                                   float referenceFeetY) const
{
  return Collision.QueryGroundFeetYUnder(worldX, worldZ, referenceFeetY);
}

bool UWorld::IsValidStandCell(const glm::ivec3 &cell,
                              const PlayerCapsule &cap) const
{
  return Collision.IsValidStandCell(cell, cap);
}

bool UWorld::IsValidStandFootprint(const glm::vec3 &eyePos,
                                   const PlayerCapsule &cap, float feetY) const
{
  return Collision.IsValidStandFootprint(eyePos, cap, feetY);
}

void UWorld::EnsurePlayerOnGround()
{
  auto user = GetCurrentUser();
  if (!user || !BlockRegistry)
  {
    return;
  }

  std::string userName = CurrentUserName;
  for (const auto &entry : Users)
  {
    if (entry.second == user)
    {
      userName = entry.first;
      break;
    }
  }
  auto camera = GetUserCamera(userName);
  if (!camera)
  {
    return;
  }

  const PlayerCapsule cap = PlayerCapsule::Standing();
  glm::vec3 pos = user->GetPosition();
  const glm::ivec3 column = WorldPosToBlock(pos);
  int x = column.x;
  int z = column.z;

  std::optional<int> topY = FindHighestSolidY(x, z);
  if (!topY)
  {
    const glm::ivec3 spawnColumn = WorldPosToBlock(SpawnPoint);
    x = spawnColumn.x;
    z = spawnColumn.z;
    topY = FindHighestSolidY(x, z);
  }
  if (!topY)
  {
    ApplySpawnToCamera();
    if (auto cam = GetUserCamera(userName))
    {
      cam->ResetVerticalPhysics();
    }
    return;
  }

  pos = BlockCenter(glm::ivec3(x, *topY, z));
  pos.y = BlockTopY(*topY) + cap.eyeHeight;
  DepenetrateEye(pos, cap, GetMovementCollisionSkipId());

  user->SetPosition(pos);
  camera->SetPosition(pos);
  camera->ResetVerticalPhysics();
}

void UWorld::WarmupVisibleListAtCamera()
{
  auto camera = GetCurrentUserCamera();
  if (!camera)
  {
    return;
  }
  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  const glm::mat4 vp = proj * view;
  MeshService->UpdateVisibleInstances(Frustum::FromViewProjection(vp), vp,
                                      camera->GetPosition());
}

void UWorld::WarmupSpawnAreaForEnterGame()
{
  Streaming->WarmupSpawnAreaForEnterGame(*this);
}

void UWorld::FinalizePlayerAfterWorldLoad()
{
  ResetMeshLoadDiagnostics();
  BlockCounter.MarkNeedsRecount();
  bool has_terrain_chunks = false;
  BlockWorld.GetChunkManager().ForEachChunk([&](const UChunk &)
                                            { has_terrain_chunks = true; });
  BlockWorldReady = has_terrain_chunks || CachedBlockCount > 0;
  PhysicsSuspendFrames = 3;

  if (CurrentUserName.empty() && !Users.empty())
  {
    SetCurrentUserName(Users.begin()->first);
  }
  if (Environment.GetControlledCreatureId() == 0)
  {
    if (Environment.GetPlayerCreatureId() != 0)
    {
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
    else if (auto user = GetCurrentUser())
    {
      if (user->GetPlayerCreatureId() != 0)
      {
        Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
        SetControlledCreature(Environment.GetPlayerCreatureId());
      }
    }
  }

  if (auto user = GetCurrentUser())
  {
    SanitizeUserPosition(user);
    if (BlockWorldReady)
    {
      EnsurePlayerOnGround();
    }
    else
    {
      ApplySpawnToCamera();
    }
  }
  else
  {
    ApplySpawnToCamera();
  }

  if (auto camera = GetCurrentUserCamera())
  {
    camera->ResetVerticalPhysics();
  }
}

void UWorld::ApplyUserToCamera(const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  SanitizeUserPosition(user);

  std::string userName = CurrentUserName;
  for (const auto &entry : Users)
  {
    if (entry.second == user)
    {
      userName = entry.first;
      break;
    }
  }
  if (auto camera = GetUserCamera(userName))
  {
    camera->SetPosition(user->GetPosition());
    camera->SetOrientation(user->GetCameraYaw(), user->GetCameraPitch());
  }
}

void UWorld::Create(const std::string &world_name)
{
  UNullProgressSink sink;
  BeginCooperativeCreate(world_name);
  while (!TickCooperativeCreate(sink, 64))
  {
  }
}

void UWorld::Load(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeLoad(world_folder_path);
  while (!TickCooperativeLoad(sink, 64))
  {
  }
}

void UWorld::Save(const std::string &world_folder_path)
{
  UNullProgressSink sink;
  BeginCooperativeSave(world_folder_path);
  while (!TickCooperativeSave(sink, 64))
  {
  }
}

void UWorld::BeginCooperativeLoad(const std::string &world_folder_path)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginLoad(*this, world_folder_path);
}

bool UWorld::TickCooperativeLoad(IUProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeSave(const std::string &world_folder_path)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginSave(*this, world_folder_path);
}

bool UWorld::TickCooperativeSave(IUProgressSink &sink, int chunkBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, chunkBudget);
}

void UWorld::BeginCooperativeCreate(const std::string &world_name)
{
  if (!CoopSession)
  {
    CoopSession = std::make_unique<UWorldCooperativeSession>();
  }
  CoopSession->BeginCreate(*this, world_name);
}

bool UWorld::TickCooperativeCreate(IUProgressSink &sink, int columnBudget)
{
  if (!CoopSession)
  {
    return true;
  }
  return CoopSession->Tick(*this, sink, columnBudget);
}

bool UWorld::HasActiveCooperativeOperation() const
{
  return CoopSession && CoopSession->Active;
}

bool UWorld::AddObject(const std::string type_id, const glm::vec3 &position)
{
  if (!BlockRegistry)
  {
    return false;
  }
  const BlockId Id = BlockRegistry->GetIdByTypeName(type_id);
  if (Id == BLOCK_AIR)
  {
    std::cerr << "World::AddObject: Unknown block type '" << type_id << "'"
              << std::endl;
    return false;
  }
  const glm::ivec3 blockPos = WorldPosToBlock(position);
  if (!BlockWorld.IsAir(blockPos))
  {
    return false;
  }
  BlockWorld.SetBlock(blockPos, Id);
  if (BlockWorld.GetBlock(blockPos) != Id)
  {
    return false;
  }
  ++CachedBlockCount;
  BlockWorldReady = true;
  MarkBlockChunkDirty(blockPos);
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  if (BlockRegistry && BlockRegistry->IsLiquid(Id) && PhysicsFlags.EnableFluids)
  {
    EnqueueFluidFrontierAt(*this, blockPos);
    MarkBlockChunkDirty(blockPos);
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      MarkBlockChunkDirty(blockPos + offset);
    }
  }
  return true;
}

bool UWorld::PlaceObject(const std::string &prefab_name,
                         glm::ivec3 anchorWorldPos)
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }

  const ObjectPlacementStats stats =
      PlaceObjectAt(BlockWorld, *prefab, anchorWorldPos, true);
  if (stats.placedCount == 0)
  {
    return false;
  }
  for (const auto &voxel : prefab->voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab->anchor;
    if (BlockWorld.GetBlock(worldPos) == voxel.Id)
    {
      MarkBlockChunkDirty(worldPos);
    }
  }
  return true;
}

bool UWorld::CanPlaceObject(const std::string &prefab_name,
                            glm::ivec3 anchorWorldPos) const
{
  if (!ObjectLibrary || !BlockRegistry)
  {
    return false;
  }
  const WorldObjectDefinition *prefab = ObjectLibrary->Get(prefab_name);
  if (!prefab)
  {
    return false;
  }
  return CanPlaceObjectAt(BlockWorld, *prefab, anchorWorldPos);
}

std::optional<glm::ivec3>
UWorld::FindObjectAnchorFromView(const glm::vec3 &position,
                                 const glm::vec3 &front) const
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return std::nullopt;
  }
  glm::ivec3 normal = hit->faceNormal;
  if (normal == glm::ivec3(0))
  {
    const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
    if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
        std::abs(toCamera.x) >= std::abs(toCamera.z))
    {
      normal.x = toCamera.x > 0.0f ? 1 : -1;
    }
    else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
    {
      normal.y = toCamera.y > 0.0f ? 1 : -1;
    }
    else
    {
      normal.z = toCamera.z > 0.0f ? 1 : -1;
    }
  }
  return hit->blockPos + normal;
}

bool UWorld::AddUser(const std::string &Name)
{
  if (Users.find(Name) != Users.end())
    return false;

  if (Name.empty())
    return false;

  Users[Name] = std::make_shared<UUser>();
  auto user = Users[Name];
  const glm::vec3 eyeOffset(0.0f, 1.62f, 0.0f);
  const glm::vec3 bodyOrigin = BodyOriginFromEye(SpawnPoint, eyeOffset);
  std::string speciesId = "human";
  if (const auto &creature_definitions = GetCreatureDefinitionStorage())
  {
    const std::string controlled =
        creature_definitions->GetControlledDefaultSpeciesId();
    if (!controlled.empty())
    {
      speciesId = controlled;
    }
  }
  const CreatureId pid = SpawnCreature(speciesId, bodyOrigin);
  user->SetPlayerCreatureId(pid);
  if (UPlayer *player = dynamic_cast<UPlayer *>(GetCreature(pid)))
  {
    player->BindUser(user);
    if (!user->GetSelectedSkinId().empty())
    {
      player->SetSkinId(user->GetSelectedSkinId());
      if (const CreatureDefinition *def = GetCreatureDefinition(speciesId))
      {
        player->SetVisual(CreateCreatureVisual(*def));
      }
    }
    UCreatureInventory &inv = player->GetInventory();
    inv.InitCreativeDefaults();
    inv.EnsureDefaultHotbar();
  }
  if (Users.size() == 1)
  {
    Environment.SetPlayerCreatureId(pid);
    Environment.SetControlledCreatureId(pid);
  }
  auto camera = std::make_shared<UCamera>(
      SpawnPoint, glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f);
  camera->SetFreeMove(false);
  const size_t viewId = ViewInstance->AddCameraReturnId(camera);
  user->SetViewId(viewId);
  if (Users.size() == 1)
  {
    SetCurrentUserName(Name);
    ViewInstance->SetActiveCamera(viewId);
  }

  return true;
}

void UWorld::DelUser(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return;

  Users.erase(Name);
}

std::shared_ptr<UUser> UWorld::GetUser(const std::string &Name)
{
  auto I = Users.find(Name);
  return (I != Users.end()) ? I->second : nullptr;
}

const std::string &UWorld::GetCurrentUserName() const
{
  return CurrentUserName;
}

std::shared_ptr<UUser> UWorld::GetCurrentUser()
{
  return GetUser(CurrentUserName);
}

std::shared_ptr<UUser> UWorld::GetCurrentUser() const
{
  return const_cast<UWorld *>(this)->GetUser(CurrentUserName);
}

UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user)
{
  if (!user || user->GetPlayerCreatureId() == 0)
  {
    return nullptr;
  }
  if (UCreature *creature = GetCreature(user->GetPlayerCreatureId()))
  {
    return &creature->GetInventory();
  }
  return nullptr;
}

const UCreatureInventory *
UWorld::GetPlayerInventory(const std::shared_ptr<UUser> &user) const
{
  return const_cast<UWorld *>(this)->GetPlayerInventory(user);
}

void UWorld::EnsurePlayerHotbarCount(const std::shared_ptr<UUser> &user,
                                     size_t barCount)
{
  if (UCreatureInventory *inv = GetPlayerInventory(user))
  {
    inv->EnsureHotbarCount(barCount);
  }
}

bool UWorld::SetCurrentUserName(const std::string &Name)
{
  if (Users.find(Name) == Users.end())
    return false;
  CurrentUserName = Name;
  if (auto user = GetCurrentUser())
  {
    if (user->GetPlayerCreatureId() != 0)
    {
      Environment.SetPlayerCreatureId(user->GetPlayerCreatureId());
      SetControlledCreature(Environment.GetPlayerCreatureId());
    }
  }
  ApplyUserToCamera(GetCurrentUser());
  if (auto user = GetCurrentUser())
  {
    ViewInstance->SetActiveCamera(user->GetViewId());
  }
  return true;
}

std::shared_ptr<UCamera> UWorld::GetUserCamera(const std::string &Name)
{
  auto user = GetUser(Name);
  if (user == nullptr)
    return nullptr;

  return ViewInstance->GetCamera(user->GetViewId());
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera()
{
  auto user = GetCurrentUser();
  if (user == nullptr)
    return nullptr;

  return ViewInstance->GetCamera(user->GetViewId());
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera() const
{
  auto user = GetCurrentUser();
  if (user == nullptr)
  {
    return nullptr;
  }
  return ViewInstance->GetCamera(user->GetViewId());
}

bool UWorld::AddObjectByView()
{
  return AddObjectByView(GetCurrentUserCamera()->GetPosition(),
                         GetCurrentUserCamera()->GetFront());
}

bool UWorld::DelObjectByView()
{
  return DelObjectByView(GetCurrentUserCamera()->GetPosition(),
                         GetCurrentUserCamera()->GetFront());
}

bool UWorld::CheckRayIntersection(
    const glm::vec3 &position, const glm::vec3 &front,
    std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
        &distance_map) const
{
  distance_map.clear();
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  const glm::vec3 hitCenter = BlockCenter(hit->blockPos);
  distance_map[hit->distance] =
      std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>(
          0, glm::vec3(hit->faceNormal), hitCenter, 0, 0);
  return true;
}

bool UWorld::CheckRayIntersection(const glm::vec3 &position,
                                  const glm::vec3 &front,
                                  glm::vec3 &intersecion, float &distance,
                                  size_t &cube_index, int &cube_side,
                                  size_t &object_index) const
{
  std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
      distance_map;

  const bool result = CheckRayIntersection(position, front, distance_map);
  if (result)
  {
    cube_side = std::get<0>(distance_map.begin()->second);
    intersecion = std::get<2>(distance_map.begin()->second);
    distance = distance_map.begin()->first;
    cube_index = 0;
    object_index = 0;
  }
  return result;
}

bool UWorld::CheckPositionFree(const glm::vec3 &position, float size) const
{
  return Collision.CheckPositionFree(position, size);
}

std::optional<glm::vec3>
UWorld::FindNearestFreeCubePosition(const glm::vec3 &position,
                                    const glm::vec3 &front,
                                    const PlayerCapsule &cap) const
{
  return Collision.FindNearestFreeCubePosition(position, front, cap);
}

bool UWorld::AddObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  auto user = GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const std::string &blockType =
      controlled->GetInventory().GetActiveBlockTypeName();
  if (blockType.empty())
  {
    return false;
  }

  PlayerCapsule cap = PlayerCapsule::Standing();
  if (const auto camera = GetCurrentUserCamera())
  {
    cap = camera->GetPlayerCapsule();
  }

  auto object_pos = FindNearestFreeCubePosition(position, front, cap);
  const auto fluid_target = RaycastFluidPlacementTarget(
      BlockWorld, *BlockRegistry, position, front, 8.0f);
  if (fluid_target.has_value())
  {
    const glm::vec3 fluid_center = BlockCenter(fluid_target->block_pos);
    if (!object_pos.has_value() ||
        WorldPosToBlock(*object_pos) != fluid_target->block_pos)
    {
      object_pos = fluid_center;
    }
  }
  if (object_pos.has_value())
  {
    if (AddObject(blockType, object_pos.value()))
    {
      UpdateIntersection(position, front);
      return true;
    }
  }
  return false;
}

bool UWorld::PlaceActiveObjectByView()
{
  return PlaceActiveObjectByView(GetCurrentUserCamera()->GetPosition(),
                                 GetCurrentUserCamera()->GetFront());
}

bool UWorld::PlaceActiveObjectByView(const glm::vec3 &position,
                                     const glm::vec3 &front)
{
  auto user = GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const std::string &prefabName =
      controlled->GetInventory().GetActiveObjectName();
  if (prefabName.empty())
  {
    return false;
  }

  const auto anchor = FindObjectAnchorFromView(position, front);
  if (!anchor.has_value())
  {
    return false;
  }
  if (PlaceObject(prefabName, anchor.value()))
  {
    UpdateIntersection(position, front);
    return true;
  }
  return false;
}

bool UWorld::DelBlockAt(glm::ivec3 blockPos)
{
  if (!BlockRegistry)
  {
    return false;
  }
  if (BlockWorld.GetBlock(blockPos) == BLOCK_AIR)
  {
    return false;
  }
  BlockWorld.SetBlock(blockPos, BLOCK_AIR);
  if (CachedBlockCount > 0)
  {
    --CachedBlockCount;
  }
  MarkBlockChunkDirty(blockPos);
  PublishBlockPhysicsEvent(blockPos);
  PublishNeighborPhysicsEvents(blockPos);
  if (BlockRegistry && PhysicsFlags.EnableFluids)
  {
    EnqueueFluidFrontierAt(*this, blockPos);
    MarkBlockChunkDirty(blockPos);
    if (BlockWorld.IsAir(blockPos) && BlockPhysicsService)
    {
      BlockPhysicsService->PublishFluid(blockPos);
    }
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      const glm::ivec3 neighbor = blockPos + offset;
      if (BlockRegistry->IsLiquid(BlockWorld.GetBlock(neighbor)))
      {
        MarkBlockChunkDirty(neighbor);
      }
    }
  }
  if (auto camera = GetCurrentUserCamera())
  {
    UpdateIntersection(camera->GetPosition(), camera->GetFront());
  }
  return true;
}

bool UWorld::DelObjectByView(const glm::vec3 &position, const glm::vec3 &front)
{
  const auto hit =
      RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front);
  if (!hit)
  {
    return false;
  }
  return DelBlockAt(hit->blockPos);
}

void UWorld::StartBreakSession(glm::ivec3 blockPos)
{
  BlockBreakSession session;
  session.blockPos = blockPos;
  session.progress = 0.f;
  BreakSession = session;
}

void UWorld::CancelBreakSession() { BreakSession.reset(); }

void UWorld::TickBreakSession(float dt, float durationSeconds)
{
  if (!BreakSession || durationSeconds <= 0.f)
  {
    return;
  }
  BreakSession->progress =
      std::min(1.f, BreakSession->progress + dt / durationSeconds);
}

bool UWorld::CompleteBreakSession()
{
  if (!BreakSession)
  {
    return false;
  }
  const glm::ivec3 pos = BreakSession->blockPos;
  BreakSession.reset();
  return DelBlockAt(pos);
}

float UWorld::GetBreakProgress() const
{
  return BreakSession ? BreakSession->progress : 0.f;
}

std::optional<glm::ivec3> UWorld::GetBreakSessionBlockPos() const
{
  if (!BreakSession)
  {
    return std::nullopt;
  }
  return BreakSession->blockPos;
}

bool UWorld::IsCameraInsideFluid(const glm::vec3 &eye, BlockId *outFluid) const
{
  if (!BlockRegistry)
  {
    return false;
  }
  const glm::ivec3 cell = WorldPosToBlock(eye);
  const BlockId Id = BlockWorld.GetBlock(cell);
  if (Id == BLOCK_AIR || BlockRegistry->BlocksMovement(Id))
  {
    return false;
  }
  const glm::vec3 center = BlockCenter(cell);
  const glm::vec3 rel = eye - center;
  if (std::abs(rel.x) > 0.5f || std::abs(rel.y) > 0.5f ||
      std::abs(rel.z) > 0.5f)
  {
    return false;
  }
  if (outFluid)
  {
    *outFluid = Id;
  }
  return true;
}

UWorld::SampledFluidState
UWorld::SampleFluidPhysicsVolume(const CollisionVolume &vol) const
{
  SampledFluidState state;
  if (!BlockRegistry)
  {
    return state;
  }
  std::unordered_map<BlockId, int> fluidWeights;
  const glm::vec3 center = vol.center;
  const glm::vec3 half = vol.halfExtents;
  const glm::ivec3 blockCenterCell = WorldPosToBlock(center);
  const int radius =
      static_cast<int>(std::ceil(std::max({half.x, half.y, half.z})));
  const glm::vec3 blockHalf(0.5f);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 blockPos = blockCenterCell + glm::ivec3(dx, dy, dz);
        const BlockId Id = BlockWorld.GetBlock(blockPos);
        if (Id == BLOCK_AIR || BlockRegistry->BlocksMovement(Id))
        {
          continue;
        }
        const glm::vec3 blockCenter = BlockCenter(blockPos);
        if (!UCube::CheckAabbCollision(center, half, blockCenter, blockHalf))
        {
          continue;
        }
        const auto &mov = BlockRegistry->Physics(Id).Movement;
        state.inFluid = true;
        fluidWeights[Id] += 1;
        state.DragHorizontal =
            std::max(state.DragHorizontal, mov.DragHorizontal);
        state.SinkSpeed = std::max(state.SinkSpeed, mov.SinkSpeed);
        state.RiseSpeed = std::max(state.RiseSpeed, mov.RiseSpeed);
      }
    }
  }
  if (state.inFluid)
  {
    state.blendWeight = 1.0f;
    int bestWeight = 0;
    for (const auto &entry : fluidWeights)
    {
      if (entry.second > bestWeight)
      {
        bestWeight = entry.second;
        state.dominantFluid = entry.first;
      }
    }
  }
  return state;
}

bool UWorld::IsFoliageFluidBlock(BlockId id) const
{
  if (!BlockRegistry || id == BLOCK_AIR)
  {
    return false;
  }
  const auto &mov = BlockRegistry->Physics(id).Movement;
  return mov.Occupancy < 1.0f && mov.SinkSpeed == 0.0f &&
         mov.RiseSpeed == 0.0f && mov.DragHorizontal == 0.0f;
}

UWorld::SampledFluidState
UWorld::SampleFluidPhysics(const glm::vec3 &eyePos,
                           const PlayerCapsule &cap) const
{
  return SampleFluidPhysicsVolume(CollisionVolumeFromEye(eyePos, cap));
}

bool UWorld::CheckBlockCollisionVolume(const CollisionVolume &vol) const
{
  return Collision.CheckBlockCollisionVolume(vol);
}

bool UWorld::CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                          CreatureId skipCreatureId) const
{
  return Collision.CheckCreatureCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollisionVolume(const CollisionVolume &vol,
                                  CreatureId skipCreatureId) const
{
  return Collision.CheckCollisionVolume(vol, skipCreatureId);
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const
{
  return CheckCollision(eyePos, cap, GetMovementCollisionSkipId());
}

bool UWorld::CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.CheckCollision(eyePos, cap, skipCreatureId);
}

bool UWorld::DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                            CreatureId skipCreatureId) const
{
  return Collision.DepenetrateEye(eyePos, cap, skipCreatureId);
}

bool UWorld::HasGroundSupportVolume(const CollisionVolume &vol,
                                    float feetY) const
{
  return Collision.HasGroundSupportVolume(vol, feetY);
}

bool UWorld::HasGroundSupport(const glm::vec3 &eyePos,
                              const PlayerCapsule &cap) const
{
  return Collision.HasGroundSupport(eyePos, cap);
}

glm::vec3 UWorld::ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                      const glm::vec3 &delta,
                                      const glm::vec3 &currentSizeBlocks,
                                      CreatureId skipCreatureId) const
{
  return Collision.ResolveMovementBody(bodyOrigin, delta, currentSizeBlocks,
                                       skipCreatureId);
}

glm::vec3 UWorld::ResolveMovement(const glm::vec3 &eyePos,
                                  const glm::vec3 &delta,
                                  const PlayerCapsule &cap,
                                  CreatureId skipCreatureId) const
{
  return Collision.ResolveMovement(eyePos, delta, cap, skipCreatureId);
}

UWorldCollision::StepUpProbe UWorld::ProbeStepUp(const glm::vec3 &eyePos,
                                                 const glm::vec3 &horiz,
                                                 const PlayerCapsule &cap,
                                                 float maxTriggerDistance) const
{
  return Collision.ProbeStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

bool UWorld::GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                              const PlayerCapsule &cap,
                              float maxTriggerDistance,
                              glm::vec3 &outLanding) const
{
  return Collision.GetStepUpLanding(eyePos, horiz, cap, maxTriggerDistance,
                                    outLanding);
}

bool UWorld::TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                       const PlayerCapsule &cap, float maxTriggerDistance) const
{
  return Collision.TryStepUp(eyePos, horiz, cap, maxTriggerDistance);
}

void UWorld::LoadUsers(const std::string &file_name)
{
  Persistence->LoadUsers(*this, file_name);
}

void UWorld::SaveUsers(const std::string &file_name)
{
  Persistence->SaveUsers(*this, file_name);
}

void UWorld::LoadWorldData(const std::string &file_name)
{
  Persistence->LoadWorldData(*this, file_name);
}

void UWorld::SaveWorldData(const std::string &file_name)
{
  Persistence->SaveWorldData(*this, file_name);
}

void UWorld::SaveMovementDiagnostics(const std::string &file_name) const
{
  UMovementDiagnosticsRecorder::SaveToFile(*this, file_name);
}

void UWorld::ConfigurePhysicsServices()
{
  BlockPhysicsService = std::make_unique<UWorldBlockPhysicsService>();
  MovementPhysicsService = std::make_unique<UWorldMovementPhysicsService>();
  ChunkDirtyService = std::make_unique<UWorldChunkDirtyService>();
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(PhysicsBudgetConfig);
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(PhysicsBudgetConfig);
  }
  PhysicsScheduler = std::make_unique<UWorldPhysicsScheduler>(
      MovementPhysicsService.get(), BlockPhysicsService.get(),
      ChunkDirtyService.get());
  Collision.SetTelemetry(&PhysicsTelemetryData);
}

void UWorld::SetPhysicsProfile(PhysicsProfile profile)
{
  ActivePhysicsProfile = profile;
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsFeatureFlags(const PhysicsFeatureFlags &flags)
{
  PhysicsFlags = flags;
  Collision.SetBroadphaseEnabled(flags.EnableCollisionBroadphase);
  Collision.SetCollisionDdaEnabled(flags.EnableCollisionDda);
  Collision.SetTelemetry(&PhysicsTelemetryData);
  if (BlockPhysicsService)
  {
    UPhysicsProfileFactory::ConfigureService(
        ActivePhysicsProfile, *BlockPhysicsService, PhysicsFlags);
  }
}

void UWorld::SetPhysicsBudgets(const PhysicsBudgets &budgets)
{
  PhysicsBudgetConfig = budgets;
  if (BlockPhysicsService)
  {
    BlockPhysicsService->SetBudgets(budgets);
  }
  if (ChunkDirtyService)
  {
    ChunkDirtyService->SetBudgets(budgets);
  }
}

void UWorld::UpdatePhysicsQueueStats(const BlockUpdateQueueStats &blockStats,
                                     const FluidUpdateSetStats &fluidStats)
{
  PhysicsTelemetryData.BlockQueueDepth = blockStats.Depth;
  PhysicsTelemetryData.LiquidQueueDepth = fluidStats.Depth;
  PhysicsTelemetryData.DeferredUpdates = blockStats.Deferred;
  PhysicsTelemetryData.DroppedUpdates = blockStats.Dropped + fluidStats.Dropped;
  PhysicsTelemetryData.PurgedUpdates = blockStats.Purged;
}

void UWorld::AccumulateFallingStats(const FallingBlocksStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Deferred;
  PhysicsTelemetryData.DroppedUpdates += stats.Dropped;
}

void UWorld::AccumulateFluidStats(const FluidSpreadStats &stats)
{
  PhysicsTelemetryData.DeferredUpdates += stats.Candidates - stats.Applied;
  PhysicsTelemetryData.DroppedUpdates += 0;
  (void)stats;
}

bool UWorld::IsWithinLiquidUpdateRadius(glm::ivec3 blockPos) const
{
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  const glm::ivec3 focus = MovementDiag.feetChunk;
  const int radius = std::max({std::abs(chunkCoord.x - focus.x),
                               std::abs(chunkCoord.y - focus.y),
                               std::abs(chunkCoord.z - focus.z)});
  return radius <= PhysicsBudgetConfig.LiquidUpdateRadiusChunks;
}

void UWorld::MarkFluidRegionDirty(glm::ivec3 center, int block_radius)
{
  const int radius = std::max(0, block_radius);
  if (!IsWithinLiquidUpdateRadius(center))
  {
    MarkBlockChunkDirtyFromPhysics(center);
    return;
  }

  std::unordered_set<glm::ivec3, IVec3Hash> chunk_coords;
  const auto add_block = [&](glm::ivec3 block_pos)
  {
    chunk_coords.insert(UChunkManager::WorldToChunk(block_pos));
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      chunk_coords.insert(UChunkManager::WorldToChunk(block_pos + offset));
    }
  };

  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        add_block(center + glm::ivec3(dx, dy, dz));
      }
    }
  }

  ModifiedChunks.insert(chunk_coords.begin(), chunk_coords.end());
  const bool immediate = BlockRegistry != nullptr;
  for (const glm::ivec3 &chunk_coord : chunk_coords)
  {
    if (immediate)
    {
      MeshService->RebuildChunkImmediate(BlockWorld, *BlockRegistry,
                                         chunk_coord);
    }
    else
    {
      MeshService->MarkDirty(chunk_coord);
    }
  }
}

void UWorld::MarkFluidChangeDirty(glm::ivec3 blockPos)
{
  if (IsWithinLiquidUpdateRadius(blockPos))
  {
    MarkBlockChunkDirty(blockPos);
  }
  else
  {
    MarkBlockChunkDirtyFromPhysics(blockPos);
  }
}

void UWorld::TryEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  const UBlockDefinitionStorage *definitions = BlockRegistry->GetDefinitions();
  if (definitions == nullptr ||
      !UFluidSpreadSystem::HasSpreadTarget(BlockWorld, *definitions, blockPos))
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
}

void UWorld::ForceEnqueueFluidAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFluids)
  {
    return;
  }
  if (BlockRegistry->IsLiquid(BlockWorld.GetBlock(blockPos)))
  {
    BlockPhysicsService->PublishFluid(blockPos);
    return;
  }
  const UBlockDefinitionStorage *definitions = BlockRegistry->GetDefinitions();
  if (definitions != nullptr &&
      UFluidSpreadSystem::HasSpreadTarget(BlockWorld, *definitions, blockPos))
  {
    BlockPhysicsService->PublishFluid(blockPos);
  }
}

void UWorld::WakeFluidFrontier(glm::ivec3 blockPos, int radius_blocks)
{
  if (!BlockRegistry || !PhysicsFlags.EnableFluids || !BlockPhysicsService)
  {
    return;
  }
  BlockPhysicsService->PublishFluid(blockPos);
  const int clamped_radius = std::max(0, radius_blocks);
  for (int dx = -clamped_radius; dx <= clamped_radius; ++dx)
  {
    for (int dy = -clamped_radius; dy <= clamped_radius; ++dy)
    {
      for (int dz = -clamped_radius; dz <= clamped_radius; ++dz)
      {
        if (dx == 0 && dy == 0 && dz == 0)
        {
          continue;
        }
        const glm::ivec3 pos(blockPos.x + dx, blockPos.y + dy, blockPos.z + dz);
        if (BlockRegistry->IsLiquid(BlockWorld.GetBlock(pos)))
        {
          ForceEnqueueFluidAt(pos);
        }
      }
    }
  }
}

void UWorld::TrySeedFallingAt(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService || !BlockRegistry || !PhysicsFlags.EnableFalling)
  {
    return;
  }
  if (!BlockRegistry->IsFallingBlock(BlockWorld.GetBlock(blockPos)))
  {
    return;
  }
  const glm::ivec3 chunk_coord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishSupportLost(
      blockPos, chunk_coord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
}

void UWorld::PublishBlockPhysicsEvent(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  BlockPhysicsService->PublishBlockChanged(
      blockPos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
  TryEnqueueFluidAt(blockPos);
}

void UWorld::PublishNeighborPhysicsEvents(glm::ivec3 blockPos)
{
  if (!BlockPhysicsService)
  {
    return;
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 pos = blockPos + offset;
    const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(pos);
    BlockPhysicsService->PublishNeighborChanged(
        pos, chunkCoord, PhysicsTickCounter, ++PhysicsEventOrderCounter);
    TryEnqueueFluidAt(pos);
  }
  const glm::ivec3 above(blockPos.x, blockPos.y + 1, blockPos.z);
  const glm::ivec3 aboveChunk = UChunkManager::WorldToChunk(above);
  BlockPhysicsService->PublishSupportLost(above, aboveChunk, PhysicsTickCounter,
                                          ++PhysicsEventOrderCounter);
}

bool UWorld::IsCollisionReadyAtFeet(const glm::ivec3 &feetBlock) const
{
  if (!PhysicsFlags.EnableCollisionReadinessGate || !Streaming ||
      !Streaming->HasStreamer())
  {
    return true;
  }
  if (const auto *streamer = Streaming->GetStreamer())
  {
    return streamer->IsCollisionReady(
        feetBlock, PhysicsBudgetConfig.CollisionSafetyRadiusChunks);
  }
  return false;
}

void UWorld::DoMovement()
{
  ++PhysicsTickCounter;
  if (PhysicsScheduler)
  {
    auto t_begin = std::chrono::high_resolution_clock::now();
    PhysicsScheduler->Tick(*this);
    auto t_end = std::chrono::high_resolution_clock::now();
    PhysicsTelemetryData.PhysicsStepMs =
        std::chrono::duration<double, std::milli>(t_end - t_begin).count();
    return;
  }
  RunLegacyPhysicsFrame();
}

void UWorld::RunLegacyPhysicsFrame()
{
  if (!BlockWorldReady)
  {
    return;
  }
  if (PhysicsSuspendFrames > 0)
  {
    --PhysicsSuspendFrames;
    return;
  }

  auto t_begin = std::chrono::high_resolution_clock::now();
  Streaming->ResetFrameTiming();

  auto camera = GetCurrentUserCamera();
  UCreature *controlled = GetControlledCreature();
  if (camera && camera->GetPosition().y < kMinReasonablePlayerY)
  {
    EnsurePlayerOnGround();
    camera->ResetVerticalPhysics();
    return;
  }
  const float prevPlayerY = camera ? camera->GetPosition().y : 0.0f;
  const float dt = camera ? camera->GetDeltaTime() : 0.0f;
  glm::ivec3 feetBlockForReadiness(0);
  bool hasFeetBlockForReadiness = false;

  if (camera && Streaming->HasStreamer() && IsStreamingEnabled())
  {
    const glm::vec3 eyePos = camera->GetPosition();
    float feetY =
        FeetYFromEye(eyePos, controlled ? controlled->GetEyeOffset().y : 1.62f);
    if (controlled)
    {
      feetY = BoundsFeetY(controlled->GetBodyOrigin());
    }
    const glm::ivec3 feetBlock =
        WorldPosToBlock(glm::vec3(eyePos.x, feetY + 0.01f, eyePos.z));
    feetBlockForReadiness = feetBlock;
    hasFeetBlockForReadiness = true;
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    Streaming->EnsureCollisionChunks(feetBlock, forward);
  }

  UWorldCreatureActivitySink activitySink(*this);
  Environment.TickActivity(*this, activitySink, dt);

  ForEachCreature(
      [&](UCreature &creature)
      {
        if (Environment.GetControlledCreatureId() != 0 &&
            creature.GetId() == Environment.GetControlledCreatureId())
        {
          return;
        }
        if (creature.IsPossessed())
        {
          return;
        }
        creature.ExecuteIntent(*this, dt);
      });

  bool is_moved = camera && camera->DoMovement(this);
  static bool was_collision_ready = true;
  const bool collision_ready = !hasFeetBlockForReadiness ||
                               IsCollisionReadyAtFeet(feetBlockForReadiness);
  if (!collision_ready && camera)
  {
    PhysicsTelemetryData.CollisionReadyWaitMs +=
        static_cast<double>(camera->GetDeltaTime()) * 1000.0;
  }
  if (collision_ready != was_collision_ready)
  {
    ++PhysicsTelemetryData.CollisionReadyTransitions;
    was_collision_ready = collision_ready;
  }
  if (camera && hasFeetBlockForReadiness && !collision_ready)
  {
    camera->ResetVerticalPhysics();
    is_moved = true;
  }
  if (Streaming && Streaming->GetStreamer() && hasFeetBlockForReadiness)
  {
    const glm::ivec3 feet_chunk =
        UChunkManager::WorldToChunk(feetBlockForReadiness);
    Streaming->GetStreamer()->SetCollisionUrgentRing(
        feet_chunk, PhysicsBudgetConfig.CollisionSafetyRadiusChunks,
        !collision_ready);
  }

  if (controlled && camera)
  {
    const glm::vec3 eye = camera->GetPosition();
    float feetY = FeetYFromEye(eye, controlled->GetEyeOffset().y);
    if (!camera->GetFreeMove() && camera->HasAnchoredFeet())
    {
      const int gx = WorldCoordToBlockIndex(eye.x);
      const int gz = WorldCoordToBlockIndex(eye.z);
      if (const std::optional<float> gy = QueryGroundFeetYUnder(gx, gz, feetY))
      {
        feetY = *gy;
      }
    }
    controlled->SetBodyOrigin(glm::vec3(eye.x, feetY, eye.z));
    controlled->GetLocomotion().SetStanceBlendForView(camera->GetStanceBlend());
    controlled->GetLocomotion().SyncFeetAnchorFromView(
        feetY, camera->HasAnchoredFeet());
    controlled->SetOrientation(ModelYawFromCameraYaw(camera->GetYaw()),
                               camera->GetPitch());
    controlled->SyncBoundsFromStance();
    controlled->GetLocomotion().SetMode(camera->GetFreeMove()
                                            ? CreatureMovementMode::Flying
                                            : CreatureMovementMode::Walking);
    float horizontalSpeed = 0.0f;
    const UCreatureLocomotionController &camLoc =
        camera->GetLocomotionController();
    const LocomotionState camState = camLoc.GetLocomotionState();
    const PlayerInput moveInput = camera->GetMovementInput();
    if (camState == LocomotionState::Walk || camState == LocomotionState::Run ||
        camState == LocomotionState::Crouch)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    else if (camState == LocomotionState::Fly ||
             camState == LocomotionState::Glide ||
             camState == LocomotionState::Hover)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    controlled->RebuildLocomotionFactsFromController(
        camLoc, controlled->GetLocomotion().GetCapabilities(),
        static_cast<float>(camera->GetDeltaTime()), horizontalSpeed, this);
    is_moved = true;
  }

  if (camera)
  {
    UpdateStreaming();
    TickAsyncChunkSystems();
    TickMeshEmerge();
    BlockWorldReady = true;
  }

  if (is_moved && camera)
  {
    if (auto user = GetCurrentUser())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
    UpdateIntersection(camera->GetPosition(), camera->GetFront());
  }

  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDoMovementMks = static_cast<uint64_t>(
      std::chrono::duration<double, std::micro>(t_end - t_begin).count());
  PhysicsTelemetryData.MovementStepMs =
      std::chrono::duration<double, std::milli>(t_end - t_begin).count();
  UMovementDiagnosticsRecorder::Update(*this, camera, prevPlayerY);
}

void UWorld::UpdateFrameHitchDiagnostics(double draw_scene_mks,
                                         double view_update_mks)
{
  DurationDrawSceneMks = static_cast<uint64_t>(draw_scene_mks);
  const double frameMs =
      (DurationDoMovementMks + view_update_mks + draw_scene_mks) / 1000.0;
  MovementDiag.hitchDetected = frameMs > 50.0 || MovementDiag.deltaTime > 0.1f;
}

void UWorld::ResetMeshLoadDiagnostics()
{
  FramesSinceLoad = 0;
  MeshBacklogClearedLatch = false;
  MeshLoadDiagActive = true;
}

void UWorld::TickMeshLoadDiagnostics()
{
  if (!MeshLoadDiagActive)
  {
    return;
  }
  if (FramesSinceLoad < 600 || MeshService->HasPendingDirty() ||
      MeshService->HasPendingAsyncMeshWork())
  {
    ++FramesSinceLoad;
  }
  if (!MeshBacklogClearedLatch && !MeshService->HasPendingDirty() &&
      !MeshService->HasPendingAsyncMeshWork())
  {
    MeshBacklogClearedLatch = true;
  }
  if (MeshBacklogClearedLatch && FramesSinceLoad >= 600 &&
      !MeshService->HasPendingDirty())
  {
    MeshLoadDiagActive = false;
  }
}

void UWorld::UpdateStreaming()
{
  Streaming->UpdateStreaming(*this, *MeshService, Render, RenderDistanceChunks,
                             EffectiveRenderDistance, EffectiveFogStartRatio,
                             AltitudeParams, LastCameraPosition,
                             LastMovementSpeed);
}

size_t UWorld::GetRenderInstanceCount() const
{
  if (Render.GreedyMeshing)
  {
    return MeshService->GetGreedyVertexCount();
  }
  return MeshService->GetInstanceCount();
}

void UWorld::UpdateIntersection(const glm::vec3 &position,
                                const glm::vec3 &front)
{
  IsIntersectionExists = CheckRayIntersection(
      position, front, Intersection, IntersectionDistance,
      IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);
  bool maybeSolidAlongRay = false;
  if (BlockRegistry)
  {
    maybeSolidAlongRay = TraverseVoxelRay(
        position, front, 8.0f, [&](glm::ivec3 cell)
        { return BlockRegistry->BlocksMovement(BlockWorld.GetBlock(cell)); });
  }
  const auto hit =
      maybeSolidAlongRay
          ? RaycastSolidBlocks(BlockWorld, *BlockRegistry, position, front)
          : std::nullopt;
  HasIntersectionBlock = hit.has_value();
  PlaceTargetActive = false;
  PlaceBlockPos = glm::ivec3(0);
  if (hit)
  {
    IntersectionBlockPos = hit->blockPos;
    const auto camera = GetCurrentUserCamera();
    const PlayerCapsule cap =
        camera ? camera->GetPlayerCapsule() : PlayerCapsule::Standing();
    const auto free_pos = FindNearestFreeCubePosition(position, front, cap);
    glm::ivec3 normal = hit->faceNormal;
    if (normal == glm::ivec3(0))
    {
      const glm::vec3 toCamera = position - BlockCenter(hit->blockPos);
      if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
          std::abs(toCamera.x) >= std::abs(toCamera.z))
      {
        normal.x = toCamera.x > 0.0f ? 1 : -1;
      }
      else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
      {
        normal.y = toCamera.y > 0.0f ? 1 : -1;
      }
      else
      {
        normal.z = toCamera.z > 0.0f ? 1 : -1;
      }
    }
    const glm::ivec3 place_pos = hit->blockPos + normal;
    if (free_pos.has_value() && WorldPosToBlock(*free_pos) == place_pos &&
        BlockWorld.IsAir(place_pos))
    {
      PlaceTargetActive = true;
      PlaceBlockPos = place_pos;
    }
    else if (BlockRegistry)
    {
      const auto fluid_target = RaycastFluidPlacementTarget(
          BlockWorld, *BlockRegistry, position, front, 8.0f);
      if (fluid_target.has_value() && BlockWorld.IsAir(fluid_target->block_pos))
      {
        if (!free_pos.has_value() ||
            WorldPosToBlock(*free_pos) == fluid_target->block_pos ||
            fluid_target->via_fluid_volume)
        {
          PlaceTargetActive = true;
          PlaceBlockPos = fluid_target->block_pos;
        }
      }
    }
  }
  else
  {
    IntersectionBlockPos = glm::ivec3(0);
  }

  if (auto user = GetCurrentUser())
  {
    if (auto camera = GetCurrentUserCamera())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
  }
}

bool UWorld::GetIsIntersectionExists() const { return IsIntersectionExists; }

size_t UWorld::GetIntersectionObjectIndex() const
{
  return IntersectionObjectIndex;
}

size_t UWorld::GetIntersectionCubeIndex() const
{
  return IntersectionCubeIndex;
}

uint64_t UWorld::GetDurationDoMovementMks() const
{
  return DurationDoMovementMks;
}

void UWorld::InvalidateBlockMesh()
{
  if (BlockRegistry)
  {
    MeshService->MarkAllDirtyFromWorld(BlockWorld);
  }
}

void UWorld::SetRenderSettings(const RenderSettings &settings)
{
  Render = settings;
  MeshService->SetRenderSettings(settings);
}

UWorldMeshService &UWorld::GetMeshService() { return *MeshService; }

const UWorldMeshService &UWorld::GetMeshService() const { return *MeshService; }

void UWorld::MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y)
{
  MeshService->MarkColumnMeshDirty(world_x, world_z, min_y, max_y);
}

void UWorld::MarkTerrainChunkMeshDirty(glm::ivec3 groundChunkCoord, int min_y,
                                       int max_y)
{
  MeshService->MarkTerrainChunkMeshDirty(groundChunkCoord, min_y, max_y);
}

void UWorld::MarkBlockChunkDirty(glm::ivec3 blockPos)
{
  // When BlockRegistry is available (normal gameplay), rebuild mesh
  // synchronously so block edits appear immediately. During headless load /
  // pre-registry init, defer via MarkDirty and let the frame budget rebuild
  // later.
  const glm::ivec3 chunkCoord = UChunkManager::WorldToChunk(blockPos);
  ModifiedChunks.insert(chunkCoord);

  const bool immediate = BlockRegistry != nullptr;
  auto mark_coord = [&](glm::ivec3 coord)
  {
    if (immediate)
    {
      MeshService->RebuildChunkImmediate(BlockWorld, *BlockRegistry, coord);
    }
    else
    {
      MeshService->MarkDirty(coord);
    }
  };

  mark_coord(chunkCoord);
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    mark_coord(UChunkManager::WorldToChunk(blockPos + offset));
  }
}

void UWorld::MarkBlockChunkDirtyFromPhysics(glm::ivec3 blockPos)
{
  if (ChunkDirtyService)
  {
    ChunkDirtyService->MarkVisualRemesh(*this, blockPos);
    ChunkDirtyService->MarkCollisionRebuild(*this, blockPos);
    return;
  }
  MarkBlockChunkDirty(blockPos);
}

} // namespace cutum
