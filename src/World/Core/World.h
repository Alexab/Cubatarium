#ifndef WORLD_H
#define WORLD_H

#include "Activity/IUWorldPerception.h"
#include "App/Settings/RenderSettings.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockCountTracker.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/WorldCooperativeOps.h"
#include "World/Environment/WorldEnvironment.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/Math/CollisionVolume.h"
#include "World/Physics/PhysicsProfile.h"
#include "World/Physics/PhysicsTelemetry.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <array>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
struct CreatureDefinition;

class UViewEngine;
class UTextureCubeStorage;
class UObjectLibrary;
class UUser;
class UCamera;
class UWorldMeshService;
class UChunkStorageService;
class UWorldPersistence;
class UWorldStreaming;
class IUPhysicsScheduler;
class UWorldBlockPhysicsService;
class UWorldMovementPhysicsService;
class UWorldChunkDirtyService;
struct BlockUpdateQueueStats;
struct FluidUpdateSetStats;
struct FallingBlocksStats;
struct FluidSpreadStats;

class UWorld : public IUWorldPerception
{
public:
  UWorld(std::shared_ptr<UTextureCubeStorage> texture_cube,
         std::shared_ptr<UViewEngine> views);
  ~UWorld();

  void GenerateUsers();

  std::string GetWorldName() const;
  void SetWorldName(const std::string &value);

  glm::vec3 GetSpawnPoint() const;
  void SetSpawnPoint(glm::vec3 value);

  void SetTerrainParams(uint32_t Seed, const std::string &terrainType);
  void SetProceduralSettings(const ProceduralSettings &settings,
                             bool rebuildPipeline = true);
  const ProceduralSettings &GetProceduralSettings() const
  {
    return ProceduralTemplate;
  }
  uint32_t GetWorldSeed() const { return WorldSeed; }
  const std::string &GetTerrainType() const { return TerrainType; }

  void Create(const std::string &world_name);
  void Load(const std::string &world_folder_path);
  void Save(const std::string &world_folder_path);

  void BeginCooperativeLoad(const std::string &world_folder_path);
  bool TickCooperativeLoad(class IUProgressSink &sink, int chunkBudget);
  void BeginCooperativeSave(const std::string &world_folder_path);
  bool TickCooperativeSave(class IUProgressSink &sink, int chunkBudget);
  void BeginCooperativeCreate(const std::string &world_name);
  bool TickCooperativeCreate(class IUProgressSink &sink, int columnBudget);
  bool HasActiveCooperativeOperation() const;

  std::shared_ptr<UUser> GetUser(const std::string &Name);
  bool AddUser(const std::string &Name);
  void DelUser(const std::string &Name);

  const std::string &GetCurrentUserName() const;
  std::shared_ptr<UUser> GetCurrentUser();
  std::shared_ptr<UUser> GetCurrentUser() const;
  bool SetCurrentUserName(const std::string &Name);

  UCreatureInventory *GetPlayerInventory(const std::shared_ptr<UUser> &user);
  const UCreatureInventory *
  GetPlayerInventory(const std::shared_ptr<UUser> &user) const;
  void EnsurePlayerHotbarCount(const std::shared_ptr<UUser> &user,
                               size_t barCount);

  std::shared_ptr<UCamera> GetUserCamera(const std::string &Name);
  std::shared_ptr<UCamera> GetCurrentUserCamera();
  std::shared_ptr<UCamera> GetCurrentUserCamera() const;

  const UBlockWorld &GetBlockWorld() const { return BlockWorld; }
  UBlockWorld &GetBlockWorld() { return BlockWorld; }
  UBlockRegistry &GetBlockRegistry() { return *BlockRegistry; }
  const UBlockRegistry &GetBlockRegistry() const { return *BlockRegistry; }

  UWorldMeshService &GetMeshService();
  const UWorldMeshService &GetMeshService() const;

  void WaitForPendingMeshJobs();
  void RefreshBlockRegistry();
  void OnBlockRegistryChanged();
  void OnBlockRegistryRuntimeOverlayChanged();
  void SetOnBlockRegistryChanged(std::function<void()> callback)
  {
    OnBlockRegistryChangedCallback = std::move(callback);
  }
  void OnCreatureCatalogChanged();
  void ReloadAllCreatureVisuals();
  void SetOnCreatureCatalogChanged(std::function<void()> callback)
  {
    OnCreatureCatalogChangedCallback = std::move(callback);
  }
  void SetBlockDefinitionStorage(
      std::shared_ptr<UBlockDefinitionStorage> definitions);
  void SetBlockMergeRegistry(
      std::shared_ptr<class UBlockMergeRegistry> merge_registry);

  const std::vector<std::string> &GetResourcePacksEnabled() const
  {
    return ResourcePacksEnabled;
  }
  const std::vector<std::string> &GetResourcePacksPrimary() const
  {
    return ResourcePacksPrimary;
  }
  const std::vector<std::string> &GetResourcePacksSecondary() const
  {
    return ResourcePacksSecondary;
  }
  const std::string &GetWorldgenOwnerPackId() const
  {
    return WorldgenOwnerPackId;
  }
  const std::string &GetCatalogFingerprint() const
  {
    return CatalogFingerprint;
  }
  void SetCatalogFingerprint(std::string fingerprint)
  {
    CatalogFingerprint = std::move(fingerprint);
  }
  void SetResourcePacksEnabled(const std::vector<std::string> &enabled)
  {
    ResourcePacksEnabled = enabled;
    ResourcePacksPrimary = enabled;
    ResourcePacksSecondary.clear();
  }
  void SetResourcePackSelection(const std::vector<std::string> &primary,
                                const std::vector<std::string> &secondary,
                                const std::string &worldgenOwner = {})
  {
    ResourcePacksPrimary = primary;
    ResourcePacksSecondary = secondary;
    ResourcePacksEnabled = primary;
    ResourcePacksEnabled.insert(ResourcePacksEnabled.end(), secondary.begin(),
                                secondary.end());
    WorldgenOwnerPackId = worldgenOwner;
  }
  void SetOnAfterWorldDataLoaded(std::function<void()> callback)
  {
    OnAfterWorldDataLoaded = std::move(callback);
  }

  struct SampledFluidState
  {
    bool inFluid{false};
    BlockId dominantFluid{BLOCK_AIR};
    float blendWeight{0.0f};
    float DragHorizontal{0.0f};
    float SinkSpeed{0.0f};
    float RiseSpeed{0.0f};
  };
  SampledFluidState SampleFluidPhysics(const glm::vec3 &eyePos,
                                       const PlayerCapsule &cap) const;
  /// Eye inside a liquid AABB or waterlogged permeable cell (continuous, not
  /// grid-snapped); used for underwater fog.
  bool IsCameraInsideFluid(const glm::vec3 &eye,
                           BlockId *outFluid = nullptr) const;
  void ApplySpawnToCamera();
  void FinalizePlayerAfterWorldLoad();
  void WarmupSpawnAreaForEnterGame();
  void WarmupVisibleListAtCamera();
  bool IsBlockWorldReady() const { return BlockWorldReady; }
  void InvalidateBlockMesh();

  bool AddObjectByView();
  bool PlaceActiveObjectByView();
  bool DelObjectByView();
  bool DelBlockAt(glm::ivec3 blockPos);

  void StartBreakSession(glm::ivec3 blockPos);
  void CancelBreakSession();
  void TickBreakSession(float dt, float durationSeconds);
  bool CompleteBreakSession();
  float GetBreakProgress() const;
  bool HasBreakSession() const { return BreakSession.has_value(); }
  std::optional<glm::ivec3> GetBreakSessionBlockPos() const;

  bool AddObject(const std::string type_id, const glm::vec3 &position);

  bool PlaceObject(const std::string &prefab_name, glm::ivec3 anchorWorldPos);
  bool CanPlaceObject(const std::string &prefab_name,
                      glm::ivec3 anchorWorldPos) const;
  std::optional<glm::ivec3>
  FindObjectAnchorFromView(const glm::vec3 &position,
                           const glm::vec3 &front) const;

  void SetObjectLibrary(UObjectLibrary *library) { ObjectLibrary = library; }

  WorldGenSets &GetWorldGenSets() { return WorldGenSetsData; }
  const WorldGenSets &GetWorldGenSets() const { return WorldGenSetsData; }
  void SetWorldGenSets(WorldGenSets sets);
  void SaveWorldGenSetsToDisk();
  const ObjectFeatureConfig &GetResolvedObjectFeatures() const
  {
    return ResolvedObjectFeatures;
  }
  void RebuildResolvedObjectFeatures();

  void SetCreatureDefinitionStorage(
      std::shared_ptr<UCreatureDefinitionStorage> storage);
  void
  SetSkinDefinitionStorage(std::shared_ptr<USkinDefinitionStorage> storage);
  const std::shared_ptr<UCreatureDefinitionStorage> &
  GetCreatureDefinitionStorage() const
  {
    return Environment.GetCreatureDefinitionStorage();
  }
  const std::shared_ptr<USkinDefinitionStorage> &
  GetSkinDefinitionStorage() const
  {
    return Environment.GetSkinDefinitionStorage();
  }

  UCreature *GetCreature(CreatureId Id);
  const UCreature *GetCreature(CreatureId Id) const;
  UCreature *GetControlledCreature();
  const UCreature *GetControlledCreature() const;
  UCreature *GetPlayerCreature();
  const UCreature *GetPlayerCreature() const;
  CreatureId GetControlledCreatureId() const
  {
    return Environment.GetControlledCreatureId();
  }
  CreatureId GetPlayerCreatureId() const
  {
    return Environment.GetPlayerCreatureId();
  }
  bool SetControlledCreature(CreatureId Id);
  void ApplyLocomotionDefinitionToCamera(UCamera &camera,
                                         const CreatureDefinition &def) const;
  void RegisterDefaultActivityAgents();
  void SnapCreatureFeetToGround(UCreature &creature) const;

  std::optional<ControlledCreatureInfo>
  QueryControlledCreatureInfo() const override;
  std::vector<CreatureId> CreaturesInRadius(const glm::vec3 &center,
                                            float radius) const override;
  std::vector<CreatureNeighborView>
  QueryCreatureNeighborsInRadius(const glm::vec3 &center, float radius,
                                 CreatureId skip_id) const override;
  bool CreatureVolumeClearAt(const glm::vec3 &body_origin,
                             const glm::vec3 &size_blocks,
                             CreatureId skip_id) const override;
  std::optional<glm::vec3> GetCreatureBodyOrigin(CreatureId id) const override;
  /// Top face under feet: highest solid in column at or below referenceFeetY
  /// (runtime pose).
  std::optional<float> QueryGroundFeetYUnder(int worldX, int worldZ,
                                             float referenceFeetY) const;
  /// Top face of the highest solid in the full column (spawn / load snap).
  std::optional<float> QueryGroundFeetYColumn(int worldX, int worldZ) const;
  std::optional<int> FindHighestSolidY(int x, int z) const;
  /// Stand cell is column top with standing clearance (step-up / landing
  /// validation).
  bool IsValidStandCell(const glm::ivec3 &cell, const PlayerCapsule &cap) const;
  /// Enough footprint samples on valid stand cells at `feetY` (blocks corner
  /// ledge / wall lip landings).
  bool IsValidStandFootprint(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                             float feetY) const;
  CreatureId SpawnCreature(const std::string &speciesId,
                           const glm::vec3 &bodyOrigin,
                           const std::string &skinId = "");
  bool SpawnCreatureByView(const std::string &speciesId);
  bool CanSpawnCreatureByView(const std::string &speciesId);
  std::string GetCreatureSpawnBlockedHint(const std::string &speciesId);
  bool CanCreatureOccupyAt(CreatureHabitat habitat, const glm::vec3 &bodyOrigin,
                           const glm::vec3 &sizeBlocks) const override;
  bool HabitatAllowsAt(CreatureHabitat habitat, const glm::vec3 &bodyOrigin,
                       const glm::vec3 &sizeBlocks) const override;
  bool HabitatAllowsMovementAt(CreatureHabitat habitat,
                               const glm::vec3 &bodyOrigin,
                               const glm::vec3 &sizeBlocks) const override;
  bool IsWithinActivityRange(const glm::vec3 &body_origin) const override;
  std::optional<CreatureId> PickCreatureByView(const glm::vec3 &eye,
                                               const glm::vec3 &front,
                                               float maxDistance) const;
  bool TryApplySkin(CreatureId target, const std::string &skinId,
                    std::string *outError = nullptr);
  void RemoveCreature(CreatureId Id);
  void ForEachCreature(const std::function<void(UCreature &)> &fn);
  void ForEachCreature(const std::function<void(const UCreature &)> &fn) const;
  std::string ResolveAnimationTypeId(const UCreature &creature) const;
  const CreatureDefinition *
  GetCreatureDefinition(const std::string &typeId) const;
  UCreaturePosePresenterRegistry &GetPosePresenterRegistry()
  {
    return Environment.GetPosePresenterRegistry();
  }
  const UCreaturePosePresenterRegistry &GetPosePresenterRegistry() const
  {
    return Environment.GetPosePresenterRegistry();
  }
  ResolvedCreatureAppearance
  GetResolvedAppearance(const UCreature &creature) const;

  void LoadCreatures(const std::string &file_name);
  void SaveCreatures(const std::string &file_name);

  bool CheckBlockCollisionVolume(const CollisionVolume &vol) const;
  bool CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                    CreatureId skipCreatureId) const;
  bool CheckCollisionVolume(const CollisionVolume &vol,
                            CreatureId skipCreatureId = 0) const;
  bool HasGroundSupportVolume(const CollisionVolume &vol, float feetY) const;
  /// Moves from body origin along delta (Y, X, Z axis order).
  glm::vec3 ResolveMovementBody(const glm::vec3 &bodyOrigin,
                                const glm::vec3 &delta,
                                const glm::vec3 &currentSizeBlocks,
                                CreatureId skipCreatureId = 0) const;
  SampledFluidState SampleFluidPhysicsVolume(const CollisionVolume &vol) const;

  /// `eyePos` is camera/eye position; collision AABB derived from `cap`.
  bool CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap) const;
  bool CheckCollision(const glm::vec3 &eyePos, const PlayerCapsule &cap,
                      CreatureId skipCreatureId) const;
  /// Lifts eye position until the capsule no longer intersects solids.
  bool DepenetrateEye(glm::vec3 &eyePos, const PlayerCapsule &cap,
                      CreatureId skipCreatureId = 0) const;
  /// Solid block directly under the player feet (for step-up / grounded
  /// checks).
  bool HasGroundSupport(const glm::vec3 &eyePos,
                        const PlayerCapsule &cap) const;
  /// Moves from eye position along delta with axis-separated resolution (Y,
  /// then X, then Z).
  glm::vec3 ResolveMovement(const glm::vec3 &eyePos, const glm::vec3 &delta,
                            const PlayerCapsule &cap,
                            CreatureId skipCreatureId = 0) const;

  CreatureId GetMovementCollisionSkipId() const
  {
    return Environment.GetControlledCreatureId();
  }

  using StepUpProbe = UWorldCollision::StepUpProbe;
  StepUpProbe ProbeStepUp(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                          const PlayerCapsule &cap,
                          float maxTriggerDistance) const;
  /// Landing eye position on a step when within range; false if already on the
  /// step or blocked.
  bool GetStepUpLanding(const glm::vec3 &eyePos, const glm::vec3 &horiz,
                        const PlayerCapsule &cap, float maxTriggerDistance,
                        glm::vec3 &outLanding) const;
  /// Snap onto a 1-block ledge when within `maxTriggerDistance` of its riser
  /// (instant, one frame).
  bool TryStepUp(glm::vec3 &eyePos, const glm::vec3 &horiz,
                 const PlayerCapsule &cap, float maxTriggerDistance) const;
  void DoMovement();
  void RunLegacyPhysicsFrame();
  void UpdateIntersection(const glm::vec3 &position, const glm::vec3 &front);
  void UpdateStreaming();
  size_t GetRenderInstanceCount() const;
  size_t GetCachedBlockCount() const { return CachedBlockCount; }

  bool GetIsIntersectionExists() const;
  size_t GetIntersectionObjectIndex() const;
  size_t GetIntersectionCubeIndex() const;

  bool GetIsBlockIntersectionExists() const { return HasIntersectionBlock; }
  glm::ivec3 GetIntersectionBlockPos() const { return IntersectionBlockPos; }
  glm::ivec3 GetBreakBlockPos() const { return IntersectionBlockPos; }
  bool HasPlaceTarget() const { return PlaceTargetActive; }
  glm::ivec3 GetPlaceBlockPos() const { return PlaceBlockPos; }

  uint64_t GetDurationDoMovementMks() const;
  void SetPhysicsProfile(PhysicsProfile profile);
  PhysicsProfile GetPhysicsProfile() const { return ActivePhysicsProfile; }
  void SetPhysicsFeatureFlags(const PhysicsFeatureFlags &flags);
  void SetPhysicsBudgets(const PhysicsBudgets &budgets);
  const PhysicsTelemetry &GetPhysicsTelemetry() const
  {
    return PhysicsTelemetryData;
  }
  const PhysicsBudgets &GetPhysicsBudgets() const
  {
    return PhysicsBudgetConfig;
  }
  uint64_t GetPhysicsTickCounter() const { return PhysicsTickCounter; }
  void PublishBlockPhysicsEvent(glm::ivec3 blockPos);
  void PublishNeighborPhysicsEvents(glm::ivec3 blockPos);
  void TryEnqueueFluidAt(glm::ivec3 blockPos);
  void ForceEnqueueFluidAt(glm::ivec3 blockPos);
  void WakeFluidFrontier(glm::ivec3 blockPos, int radius_blocks = 2);
  void MarkFluidRegionDirty(glm::ivec3 center, int block_radius = 1);
  void TrySeedFallingAt(glm::ivec3 blockPos);
  const PhysicsFeatureFlags &GetPhysicsFeatureFlags() const
  {
    return PhysicsFlags;
  }
  bool IsCollisionReadyAtFeet(const glm::ivec3 &feetBlock) const;

  struct MovementDiagnostics
  {
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
    double streamingGenMs{0.0};
    double streamingIoMs{0.0};
    double meshRebuildMs{0.0};
    int dirtyChunksPending{0};
    int meshRebuildsThisFrame{0};
    double flatRebuildMs{0.0};
    double countNonAirMs{0.0};
    int asyncMeshInFlight{0};
    bool asyncMeshingEnabled{true};
    int greedyCacheEntries{0};
    int framesSinceLoad{0};
    bool meshBacklogCleared{false};
    double physicsStepMs{0.0};
    uint64_t physicsBlockQueueDepth{0};
    uint64_t physicsLiquidQueueDepth{0};
    uint64_t physicsDeferredUpdates{0};
    uint64_t physicsDroppedUpdates{0};
    uint64_t physicsPurgedUpdates{0};
    uint64_t physicsCollisionBroadphaseRejects{0};
    uint64_t physicsCollisionBroadphaseFallbacks{0};
    uint64_t physicsCollisionReadyTransitions{0};
    double physicsCollisionReadyWaitMs{0.0};
    uint64_t physicsVisualRemeshBacklog{0};
    uint64_t physicsCollisionRebuildBacklog{0};
  };

  const MovementDiagnostics &GetMovementDiagnostics() const
  {
    return MovementDiag;
  }

  void SetStreamingEnabled(bool enabled);
  void SetRenderDistanceChunks(int distance);
  int GetRenderDistanceChunks() const { return RenderDistanceChunks; }
  int GetEffectiveRenderDistance() const { return EffectiveRenderDistance; }
  float GetEffectiveFogStartRatio() const { return EffectiveFogStartRatio; }
  void UpdateFrameHitchDiagnostics(double draw_scene_mks,
                                   double view_update_mks);
  void SetChunkWriteFormat(ChunkWriteFormat format);
  ChunkWriteFormat GetChunkWriteFormat() const;
  void SetMaxLoadOpsPerFrame(int value) { MaxLoadOpsPerFrame = value; }
  void SetMaxUnloadOpsPerFrame(int value) { MaxUnloadOpsPerFrame = value; }
  UChunkStorageService &GetChunkStorage();
  const UChunkStorageService &GetChunkStorage() const;
  const std::string &GetWorldFolderPath() const;
  void SetWorldFolderPath(const std::string &path);
  bool IsStreamingEnabled() const;

  void SetRenderSettings(const RenderSettings &settings);
  const RenderSettings &GetRenderSettings() const { return Render; }
  void RefreshStreamerSettings();

  void SetStepUpEnabled(bool enabled) { StepUpEnabled = enabled; }
  bool IsStepUpEnabled() const { return StepUpEnabled; }

  void SetFoliageClimbEnabled(bool enabled) { FoliageClimbEnabled = enabled; }
  bool IsFoliageClimbEnabled() const { return FoliageClimbEnabled; }
  bool IsFoliageFluidBlock(BlockId id) const;

  void SetEntityCollisionEnabled(bool enabled)
  {
    Collision.SetEntityCollisionEnabled(enabled);
  }
  bool IsEntityCollisionEnabled() const
  {
    return Collision.IsEntityCollisionEnabled();
  }

  void SetActivityTickHz(float hz);

  static bool HasPersistedTerrainOnDisk(const std::string &world_folder_path);

  void ClearCreaturesAndUsers();

  friend class UMovementDiagnosticsRecorder;

private:
  friend class UWorldCooperativeSession;
  friend class UWorldStreaming;
  friend class UWorldPersistence;
  bool CheckRayIntersection(
      const glm::vec3 &position, const glm::vec3 &front,
      std::map<float, std::tuple<int, glm::vec3, glm::vec3, size_t, size_t>>
          &distance_map) const;
  bool CheckRayIntersection(const glm::vec3 &position, const glm::vec3 &front,
                            glm::vec3 &intersecion, float &distance,
                            size_t &cube_index, int &cube_side,
                            size_t &object_index) const;

  bool CheckPositionFree(const glm::vec3 &position, float size = 1.0) const;
  std::optional<glm::vec3>
  FindNearestFreeCubePosition(const glm::vec3 &position, const glm::vec3 &front,
                              const PlayerCapsule &cap) const;

  bool AddObjectByView(const glm::vec3 &position, const glm::vec3 &front);
  bool PlaceActiveObjectByView(const glm::vec3 &position,
                               const glm::vec3 &front);
  bool DelObjectByView(const glm::vec3 &position, const glm::vec3 &front);

  void LoadUsers(const std::string &file_name);
  void SaveUsers(const std::string &file_name);
  void LinkUsersToPlayerCreatures();
  void ResetCreaturesBeforeEntityLoad();

  void LoadInitialStreamingChunks();

  void LoadWorldData(const std::string &file_name);
  void SaveWorldData(const std::string &file_name);

  void GenerateWorldBlocks();
  void RebuildBlockMesh();
  void InitStreamerCallbacks();
  void TickAsyncChunkSystems();
  void TickMeshEmerge();
  void ApplyUserToCamera(const std::shared_ptr<UUser> &user);
  bool IsReasonablePlayerPosition(const glm::vec3 &position) const;
  void SanitizeUserPosition(const std::shared_ptr<UUser> &user);
  void EnsurePlayerOnGround();
  void MarkBlockChunkDirty(glm::ivec3 blockPos);
  void MarkBlocksChunkDirtyBatch(const std::vector<glm::ivec3> &block_positions);
  void MarkBlockChunkDirtyFromPhysics(glm::ivec3 blockPos);
  void MarkFluidChangeDirty(glm::ivec3 blockPos);
  void MarkFluidFloodMeshDirty(glm::ivec3 blockPos,
                               const std::vector<glm::ivec3> &filled_blocks);
  void UpdatePhysicsQueueStats(const BlockUpdateQueueStats &blockStats,
                               const FluidUpdateSetStats &fluidStats);
  void AccumulateFallingStats(const FallingBlocksStats &stats);
  void AccumulateFluidStats(const FluidSpreadStats &stats);
  void ConfigurePhysicsServices();
  bool IsWithinLiquidUpdateRadius(glm::ivec3 blockPos) const;
  void MarkColumnMeshDirty(int world_x, int world_z, int min_y, int max_y);
  void MarkTerrainChunkMeshDirty(glm::ivec3 groundChunkCoord, int min_y,
                                 int max_y);
  void SaveMovementDiagnostics(const std::string &file_name) const;
  void ResetMeshLoadDiagnostics();
  void TickMeshLoadDiagnostics();
  void RebuildWorldGenPipeline();

  std::string WorldName;
  glm::vec3 SpawnPoint;
  std::string CurrentUserName;
  uint32_t WorldSeed{12345};
  std::string TerrainType{"heightmap"};
  ProceduralSettings ProceduralTemplate;
  std::unique_ptr<IUWorldGenPipeline> WorldGen;
  size_t CachedBlockCount{0};
  bool BlockWorldReady{false};
  int PhysicsSuspendFrames{0};
  bool AllowProceduralFill{true};
  bool HasPersistedSave{false};
  bool LoadedFromChunkSave{false};
  std::vector<std::string> ResourcePacksEnabled;
  std::vector<std::string> ResourcePacksPrimary;
  std::vector<std::string> ResourcePacksSecondary;
  std::string WorldgenOwnerPackId;
  std::string CatalogFingerprint;
  std::function<void()> OnAfterWorldDataLoaded;
  std::function<void()> OnBlockRegistryChangedCallback;
  std::function<void()> OnCreatureCatalogChangedCallback;

  std::map<std::string, std::shared_ptr<UUser>> Users;

  UWorldEnvironment Environment;

  std::shared_ptr<UTextureCubeStorage> TextureCubeInstance;
  std::shared_ptr<UViewEngine> ViewInstance;
  UObjectLibrary *ObjectLibrary{nullptr};
  WorldGenSets WorldGenSetsData;
  ObjectFeatureConfig ResolvedObjectFeatures;

  std::shared_ptr<UBlockDefinitionStorage> BlockDefinitions;
  std::shared_ptr<class UBlockMergeRegistry> BlockMergeRegistry;
  std::unique_ptr<UBlockRegistry> BlockRegistry;
  UBlockWorld BlockWorld;
  UWorldCollision Collision;
  UBlockCountTracker BlockCounter;
  std::unique_ptr<UWorldMeshService> MeshService;
  std::unique_ptr<UWorldStreaming> Streaming;
  std::unique_ptr<UWorldPersistence> Persistence;
  bool StepUpEnabled{true};
  bool FoliageClimbEnabled{true};
  RenderSettings Render;
  int RenderDistanceChunks{4};
  int EffectiveRenderDistance{4};
  float EffectiveFogStartRatio{0.85f};
  StreamingAltitudePolicyParams AltitudeParams;
  glm::vec3 LastCameraPosition{0.0f};
  float LastMovementSpeed{0.0f};
  int MaxLoadOpsPerFrame{4};
  int MaxUnloadOpsPerFrame{2};
  std::unordered_set<glm::ivec3, IVec3Hash> ModifiedChunks;
  bool IsIntersectionExists;
  glm::vec3 Intersection;
  float IntersectionDistance;
  size_t IntersectionCubeIndex;
  int IntersectionCubeSide;
  size_t IntersectionObjectIndex;

  bool HasIntersectionBlock{false};
  glm::ivec3 IntersectionBlockPos{0};
  bool PlaceTargetActive{false};
  glm::ivec3 PlaceBlockPos{0};

  struct BlockBreakSession
  {
    glm::ivec3 blockPos{0};
    float progress{0.f};
  };
  std::optional<BlockBreakSession> BreakSession;

  uint64_t DurationDoMovementMks;
  uint64_t DurationDrawSceneMks{0};
  MovementDiagnostics MovementDiag;
  std::vector<MovementDiagnostics> MovementDiagHistory;
  std::unique_ptr<UWorldCooperativeSession> CoopSession;
  float LastPlayerY{0.0f};
  bool HasLastPlayerY{false};
  int FramesSinceLoad{0};
  bool MeshBacklogClearedLatch{false};
  bool MeshLoadDiagActive{false};
  PhysicsProfile ActivePhysicsProfile{PhysicsProfile::Primitive};
  PhysicsFeatureFlags PhysicsFlags;
  PhysicsBudgets PhysicsBudgetConfig;
  PhysicsTelemetry PhysicsTelemetryData;
  uint64_t PhysicsTickCounter{0};
  uint64_t PhysicsEventOrderCounter{0};
  std::unique_ptr<class UWorldBlockPhysicsService> BlockPhysicsService;
  std::unique_ptr<class UWorldMovementPhysicsService> MovementPhysicsService;
  std::unique_ptr<class UWorldChunkDirtyService> ChunkDirtyService;
  std::unique_ptr<IUPhysicsScheduler> PhysicsScheduler;

  friend class UWorldChunkDirtyService;
  friend class UWorldBlockPhysicsService;
};

} // namespace cutum

#endif // WORLD_H
