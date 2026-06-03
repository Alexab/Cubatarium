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
#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include "BlockWorld.h"
#include "BlockDefinition.h"
#include "BlockDefinitionStorage.h"
#include "BlockRegistry.h"
#include "ChunkMeshCache.h"
#include "ChunkStreamer.h"
#include "ChunkManager.h"
#include "RenderSettings.h"
#include "ProceduralSettings.h"
#include "worldgen/IWorldGenPipeline.h"
#include "worldgen/WorldGenContext.h"
#include "CollisionVolume.h"
#include "CreatureBounds.h"
#include "PlayerCapsule.h"
#include "Creature.h"

namespace cutum {

class CreatureDefinitionStorage;
struct CreatureDefinition;

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
 void SetProceduralSettings(const ProceduralSettings& settings);
 const ProceduralSettings& GetProceduralSettings() const { return proceduralSettings_; }
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
 std::shared_ptr<User> GetCurrentUser() const;
 bool SetCurrentUserName(const std::string& name);

 std::shared_ptr<Camera> GetUserCamera(const std::string& name);
 std::shared_ptr<Camera> GetCurrentUserCamera();

 const BlockWorld& GetBlockWorld() const { return blockWorld_; }
 BlockWorld& GetBlockWorld() { return blockWorld_; }
 BlockRegistry& GetBlockRegistry() { return *blockRegistry_; }
 const BlockRegistry& GetBlockRegistry() const { return *blockRegistry_; }

 void RefreshBlockRegistry();
 void SetBlockDefinitionStorage(std::shared_ptr<BlockDefinitionStorage> definitions);

 struct SampledFluidState {
  bool inFluid{false};
  BlockId dominantFluid{BLOCK_AIR};
  float blendWeight{0.0f};
  float dragHorizontal{0.0f};
  float sinkSpeed{0.0f};
  float riseSpeed{0.0f};
 };
 SampledFluidState SampleFluidPhysics(const glm::vec3& eyePos, const PlayerCapsule& cap) const;
 /// Eye inside fluid block AABB (visual fog); does not use player collision capsule.
 bool IsCameraInsideFluid(const glm::vec3& eye, BlockId* outFluid = nullptr) const;
 void ApplySpawnToCamera();
 void FinalizePlayerAfterWorldLoad();
 bool IsBlockWorldReady() const { return blockWorldReady_; }
 void InvalidateBlockMesh();
 const std::vector<FaceInstance>& GetBlockRenderInstances();
 const std::vector<GreedyMeshBatch>& GetGreedyRenderBatches();
 size_t GetGreedyVertexCount() const;

 bool AddObjectByView();
 bool PlaceActivePrefabByView();
 bool DelObjectByView();

 bool AddObject(const std::string type_id, const glm::vec3 &position);

 bool PlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos);
 bool CanPlacePrefab(const std::string& prefab_name, glm::ivec3 anchorWorldPos) const;
 std::optional<glm::ivec3> FindPrefabAnchorFromView(const glm::vec3& position, const glm::vec3& front) const;

 void SetPrefabLibrary(PrefabLibrary* library) { prefabLibrary_ = library; }

 void SetCreatureDefinitionStorage(std::shared_ptr<CreatureDefinitionStorage> storage);

 Creature* GetCreature(CreatureId id);
 const Creature* GetCreature(CreatureId id) const;
 Creature* GetControlledCreature();
 const Creature* GetControlledCreature() const;
 Creature* GetPlayerCreature();
 CreatureId GetControlledCreatureId() const { return controlledCreatureId_; }
 CreatureId GetPlayerCreatureId() const { return playerCreatureId_; }
 bool SetControlledCreature(CreatureId id);
 CreatureId SpawnCreature(const std::string& typeId, const glm::vec3& bodyOrigin);
 void RemoveCreature(CreatureId id);
 void ForEachCreature(const std::function<void(Creature&)>& fn);
 void ForEachCreature(const std::function<void(const Creature&)>& fn) const;
 std::string ResolveAnimationTypeId(const Creature& creature) const;
 const CreatureDefinition* GetCreatureDefinition(const std::string& typeId) const;

 void LoadCreatures(const std::string& file_name);
 void SaveCreatures(const std::string& file_name);

 bool CheckCollisionVolume(const CollisionVolume& vol) const;
 bool HasGroundSupportVolume(const CollisionVolume& vol, float feetY) const;
 /// Moves from body origin along delta (Y, X, Z axis order).
 glm::vec3 ResolveMovementBody(const glm::vec3& bodyOrigin, const glm::vec3& delta,
                               const glm::vec3& currentSizeBlocks) const;
 SampledFluidState SampleFluidPhysicsVolume(const CollisionVolume& vol) const;

 /// `eyePos` is camera/eye position; collision AABB derived from `cap`.
 bool CheckCollision(const glm::vec3& eyePos, const PlayerCapsule& cap) const;
 /// Solid block directly under the player feet (for step-up / grounded checks).
 bool HasGroundSupport(const glm::vec3& eyePos, const PlayerCapsule& cap) const;
 /// Moves from eye position along delta with axis-separated resolution (Y, then X, then Z).
 glm::vec3 ResolveMovement(const glm::vec3& eyePos, const glm::vec3& delta,
                           const PlayerCapsule& cap) const;

 struct StepUpProbe {
  bool valid{false};
  float distanceToLedge{0.0f};
  glm::vec3 targetPos{0.0f};
  glm::vec3 moveDir{0.0f};
 };
 StepUpProbe ProbeStepUp(const glm::vec3& eyePos, const glm::vec3& horiz, const PlayerCapsule& cap,
                         float maxTriggerDistance) const;
 /// Landing eye position on a step when within range; false if already on the step or blocked.
 bool GetStepUpLanding(const glm::vec3& eyePos, const glm::vec3& horiz, const PlayerCapsule& cap,
                       float maxTriggerDistance, glm::vec3& outLanding) const;
 /// Snap onto a 1-block ledge when within `maxTriggerDistance` of its riser (instant, one frame).
 bool TryStepUp(glm::vec3& eyePos, const glm::vec3& horiz, const PlayerCapsule& cap,
                float maxTriggerDistance) const;
 void DoMovement();
 void UpdateIntersection(const glm::vec3& position, const glm::vec3& front);
 void UpdateStreaming();
 size_t GetRenderInstanceCount() const;
 uint64_t GetMeshRevision() const;
 uint64_t GetCullRevision() const;
 size_t GetCachedBlockCount() const { return cachedBlockCount_; }

 bool GetIsIntersectionExists() const;
 size_t GetIntersectionObjectIndex() const;
 size_t GetIntersectionCubeIndex() const;

 bool GetIsBlockIntersectionExists() const { return hasIntersectionBlock_; }
 glm::ivec3 GetIntersectionBlockPos() const { return intersectionBlockPos_; }

 uint64_t GetDurationDoMovementMks() const;

 struct MovementDiagnostics {
  float deltaTime{0.0f};
  float playerYDrop{0.0f};
  bool feetChunkLoaded{false};
  bool feetIsAir{false};
  bool feetInUnloadList{false};
  glm::ivec3 feetBlock{0};
  glm::ivec3 feetChunk{0};
  int streamingLoads{0};
  int streamingUnloads{0};
  bool hitchDetected{false};
  bool fallThroughSuspected{false};
  size_t meshDrawCount{0};
 };

 const MovementDiagnostics& GetMovementDiagnostics() const { return movementDiagnostics_; }

 void SetStreamingEnabled(bool enabled) { streamingEnabled_ = enabled; }
 void SetRenderDistanceChunks(int distance);
 bool IsStreamingEnabled() const { return streamingEnabled_; }

 void SetRenderSettings(const RenderSettings& settings);
 const RenderSettings& GetRenderSettings() const { return renderSettings_; }

 void SetStepUpEnabled(bool enabled) { stepUpEnabled_ = enabled; }
 bool IsStepUpEnabled() const { return stepUpEnabled_; }

 static bool HasPersistedTerrainOnDisk(const std::string& world_folder_path);

private:
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>& distance_map) const;
 bool CheckRayIntersection(const glm::vec3& position, const glm::vec3& front, glm::vec3& intersecion, float &distance, size_t &cube_index, int &cube_side, size_t &object_index) const;

 bool CheckPositionFree(const glm::vec3& position, float size=1.0) const;
 std::optional<glm::vec3> FindNearestFreeCubePosition(const glm::vec3& position, const glm::vec3& front) const;

 bool AddObjectByView(const glm::vec3& position, const glm::vec3& front);
 bool PlaceActivePrefabByView(const glm::vec3& position, const glm::vec3& front);
 bool DelObjectByView(const glm::vec3& position, const glm::vec3& front);

 void LoadUsers(const std::string &file_name);
 void SaveUsers(const std::string &file_name);
 void LinkUsersToPlayerCreatures();

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
 void UpdateMovementDiagnostics(const std::shared_ptr<Camera>& camera, float prevPlayerY);
 void RebuildWorldGenPipeline();

 std::string WorldName;
 glm::vec3 SpawnPoint;
 std::string CurrentUserName;
 uint32_t worldSeed_{12345};
 std::string terrainType_{"heightmap"};
 ProceduralSettings proceduralSettings_;
 std::unique_ptr<IWorldGenPipeline> worldGen_;
 size_t cachedBlockCount_{0};
 bool blockWorldReady_{false};
 int physicsSuspendFrames_{0};
 bool allowProceduralFill_{true};
 bool hasPersistedSave_{false};
 bool loadedFromChunkSave_{false};

 std::map<std::string, std::shared_ptr<User>> Users;

 std::unordered_map<CreatureId, std::unique_ptr<Creature>> creatures_;
 CreatureId nextCreatureId_{1};
 CreatureId playerCreatureId_{0};
 CreatureId controlledCreatureId_{0};
 std::shared_ptr<CreatureDefinitionStorage> creatureDefinitions_;

 std::shared_ptr<ObjectStorage> ObjectStorageInstance;
 std::shared_ptr<ViewEngine> ViewInstance;
 PrefabLibrary* prefabLibrary_{nullptr};

 std::shared_ptr<BlockDefinitionStorage> blockDefinitions_;
 std::unique_ptr<BlockRegistry> blockRegistry_;
 BlockWorld blockWorld_;
 ChunkMeshCache meshCache_;
 std::unique_ptr<ChunkStreamer> streamer_;
 bool streamingEnabled_{true};
 bool stepUpEnabled_{true};
 RenderSettings renderSettings_;
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
 MovementDiagnostics movementDiagnostics_;
 float lastPlayerY_{0.0f};
 bool hasLastPlayerY_{false};
};

}

#endif // WORLD_H
