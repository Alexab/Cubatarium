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
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Core/WorldCooperativeOps.h"
#include "World/Environment/EnvironmentConfig.h"
#include "World/Environment/WorldEnvironment.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/Math/CollisionVolume.h"
#include "World/Physics/PhysicsProfile.h"
#include "World/Physics/PhysicsTelemetry.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <chrono>
#include <array>
#include <cstdint>
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

struct RuntimeOverlayFlushResult;

/// Ground-column visual emerge lifecycle (Minecraft/Sodium-style ready gate).
enum class ColumnEmergeState : uint8_t
{
  Empty = 0,
  Generating,
  VoxelsReady,
  Lighting,
  LitReady,
  Meshing,
  RenderReady,
};

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
struct CreatureDefinition;

class UWorldViewBinding;
class UAsyncRelightBuilder;
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

struct UBackgroundQuiesceState
{
  enum class Phase : uint8_t
  {
    Start,
    StreamingOff,
    ChunkGenCancel,
    DrainIo,
    CancelWorkers,
    WaitRelight,
    WaitMesh,
    Finalize,
    Done
  };

  Phase phase{Phase::Start};
  int drainIoPasses{0};
  int waitChunkGenPasses{0};
  int waitRelightPasses{0};
  int waitMeshPasses{0};
  static constexpr int kMaxDrainIoPasses = 32;
  static constexpr int kMaxWaitPasses = 120;
};

class UWorld : public IUWorldPerception
{
public:
  enum class WeatherType
  {
    Clear = 0,
    Cloudy = 1,
    Rain = 2,
    Storm = 3,
    Snow = 4,
  };

  enum class CelestialBodyType
  {
    Sun = 0,
    Moon = 1,
  };

  struct UCelestialBodyVisual
  {
    std::string Id{"sun"};
    CelestialBodyType Type{CelestialBodyType::Sun};
    glm::vec3 Color{1.0f, 0.95f, 0.82f};
    float Intensity{1.0f};
    float AngularSizeDeg{0.53f};
    float OrbitInclinationDeg{23.0f};
    float OrbitPeriodDays{1.0f};
    float OrbitPhase{0.0f};
    float OrbitLongitudeDeg{0.0f};
    glm::vec3 DirectionWorld{0.0f, 1.0f, 0.0f};
  };

  struct EnvironmentState
  {
    float TimeOfDayNormalized{0.35f};
    float DayLengthMinutes{10.0f};
    bool TimeFrozen{false};
    WeatherType Weather{WeatherType::Clear};
    WeatherType TargetWeather{WeatherType::Clear};
    float WeatherTransitionSec{0.0f};
    float WeatherTransitionDurationSec{45.0f};
    float Cloudiness{0.0f};
    float PrecipitationIntensity{0.0f};
    float WindStrength{0.2f};
    float WeatherFogMultiplier{1.0f};
    float WeatherSkyAttenuation{1.0f};
    float DayNightFactor{1.0f};
    float MoonNightFactor{0.0f};
    float SurfaceWetness{0.0f};
    float StarVisibility{0.0f};
    float CloudCoverage{0.2f};
    float StarVisibilityOverride{-1.0f};
    float CloudCoverageOverride{-1.0f};
    std::vector<UCelestialBodyVisual> CelestialBodies;
  };

  struct LightingSettings
  {
    bool DebugEnabled{false};
    uint8_t DebugMode{0};
    float MinAmbient{0.12f};
    bool WeatherOverlayEnabled{true};
    bool WeatherParticlesEnabled{true};
    uint8_t WeatherDebugMode{0};
  };

  UWorld(std::shared_ptr<UTextureCubeStorage> texture_cube,
         std::shared_ptr<UViewEngine> views);
  ~UWorld();

  void GenerateUsers();

  std::string GetWorldName() const;
  void SetWorldName(const std::string &value);

  glm::vec3 GetSpawnPoint() const;
  void SetSpawnPoint(glm::vec3 value);
  glm::ivec3 GetPreferredLoadFocusBlock() const;

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
  /// Fast save for exit/autosave: metadata plus modified terrain only.
  void SaveSessionSnapshot(const std::string &world_folder_path,
                           bool skip_quiesce = false);

  void BeginCooperativeLoad(const std::string &world_folder_path);
  bool TickCooperativeLoad(class IUProgressSink &sink, int chunkBudget);
  void BeginCooperativeSave(const std::string &world_folder_path,
                            bool resume_streaming_after_save = true);
  bool TickCooperativeSave(class IUProgressSink &sink, int chunkBudget);
  void BeginCooperativeCreate(const std::string &world_name);
  bool TickCooperativeCreate(class IUProgressSink &sink, int columnBudget);
  bool HasActiveCooperativeOperation() const;
  void CancelCooperativeOperation();
  bool BlocksAsyncRelightDrain() const;

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

  std::shared_ptr<UViewEngine> GetViewEngine() const;

  const UBlockWorld &GetBlockWorld() const { return BlockWorld; }
  UBlockWorld &GetBlockWorld() { return BlockWorld; }
  UBlockRegistry &GetBlockRegistry() { return *BlockRegistry; }
  const UBlockRegistry &GetBlockRegistry() const { return *BlockRegistry; }

  UWorldMeshService &GetMeshService();
  const UWorldMeshService &GetMeshService() const;
  UWorldChunkDirtyService *GetChunkDirtyService()
  {
    return ChunkDirtyService.get();
  }

  void WaitForPendingMeshJobs();
  void WaitForPendingRelightJobs();
  bool WaitForPendingRelightJobsFor(std::chrono::milliseconds timeout);
  void PrepareForShutdown();
  /// Flight-sim / harness: short joins so exit cannot hang experiments.
  void PrepareForShutdownFast();
  void QuiesceBackgroundWork(
      std::chrono::milliseconds async_timeout = std::chrono::milliseconds(2000));
  void BeginBackgroundQuiesce(UBackgroundQuiesceState &state);
  /// @return true when quiesce finished.
  bool TickBackgroundQuiesce(UBackgroundQuiesceState &state,
                             std::chrono::milliseconds step_timeout,
                             class IUProgressSink *sink = nullptr);
  void EnsureStreamingActiveAfterBackgroundQuiesce();
  void ResumeAfterSessionSave();
  void RefreshBlockRegistry();
  void OnBlockRegistryChanged();
  void OnBlockRegistryRuntimeOverlayChanged(
      const RuntimeOverlayFlushResult *flush = nullptr);
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

  SampledFluidState SampleFluidPhysics(const glm::vec3 &eyePos,
                                       const PlayerCapsule &cap) const;
  FluidColumnSurface FindFluidColumnSurface(const glm::vec3 &eye) const;
  FluidColumnSurface FindFluidColumnSurfaceAt(int bx, int bz, int hintY) const;
  bool HasNearbyFluidSurface(glm::ivec3 cameraBlock,
                             int radiusBlocks = 48) const;
  /// True when eye.y is strictly below BlockTopY of the topmost liquid block
  /// in the eye column (binary; no body-in-fluid or grace terms).
  bool IsCameraInsideFluid(const glm::vec3 &eye,
                           BlockId *outFluid = nullptr) const;
  void ApplySpawnToCamera();
  void FinalizePlayerAfterWorldLoad();
  void ResetPhysicsRuntimeState();
  void WarmupSpawnAreaForEnterGame();
  void PrepareEnterGameSession();
  bool IsEnterStreamingWarmupSettled() const;
  void TickEnterStreamingWarmup(int iteration_budget);
  void WarmupVisibleListAtCamera();
  /// Build pending terrain meshes before GPU upload (returns true when ready).
  bool DrainEnterGameMeshWarmup(int budget);
  bool NeedsEnterGameMeshWarmup() const;
  bool IsCreateSpawnWarmupSettled() const;
  void DrainSpawnRadiusMeshWarmup(int budget);
  void RefreshPersistedTerrainAfterSave();
  void MarkSpawnAreaPreparedByCooperativeLoad();
  bool ConsumeSpawnAreaPreparedByCooperativeLoad();
  void ClearSpawnAreaPreparedByCooperativeLoad();
  bool IsSpawnAreaPreparedByCooperativeLoad() const
  {
    return SpawnAreaPreparedByCooperativeLoad;
  }
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
  UObjectLibrary *GetObjectLibrary() const { return ObjectLibrary; }

  WorldGenSets &GetWorldGenSets() { return WorldGenSetsData; }
  const WorldGenSets &GetWorldGenSets() const { return WorldGenSetsData; }
  void SetWorldGenSets(WorldGenSets sets);
  void SaveWorldGenSetsToDisk();
  const ObjectFeatureConfig &GetResolvedObjectFeatures() const
  {
    return ResolvedObjectFeatures;
  }
  void RebuildResolvedObjectFeatures();
  void RebuildWorldGenPipeline();

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
  glm::vec3 ResolveControlledDefaultEyeOffset() const;
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
  uint64_t GetDurationDrawSceneMks() const { return DurationDrawSceneMks; }
  uint64_t GetDurationViewUpdateMks() const { return DurationViewUpdateMks; }
  void SetWallFrameDelta(double seconds);
  double GetWallFrameDelta() const { return WallFrameDeltaSec; }
  void SetLastSwapWaitMs(double ms) { LastSwapWaitMs = ms; }
  double GetLastSwapWaitMs() const { return LastSwapWaitMs; }

  /// Per-frame phase timings (ms) for unaccounted breakdown in FramePerfMonitor.
  void SetLastInputMs(double ms) { LastInputMs = ms; }
  void SetLastAppUpdateMs(double ms) { LastAppUpdateMs = ms; }
  void SetLastWorldTickMs(double ms) { LastWorldTickMs = ms; }
  void SetLastPrepareFrameMs(double ms) { LastPrepareFrameMs = ms; }
  void SetLastPostSceneMs(double ms) { LastPostSceneMs = ms; }
  void SetLastGuiOverlayMs(double ms) { LastGuiOverlayMs = ms; }
  double GetLastInputMs() const { return LastInputMs; }
  double GetLastAppUpdateMs() const { return LastAppUpdateMs; }
  double GetLastWorldTickMs() const { return LastWorldTickMs; }
  double GetLastPrepareFrameMs() const { return LastPrepareFrameMs; }
  double GetLastPostSceneMs() const { return LastPostSceneMs; }
  double GetLastGuiOverlayMs() const { return LastGuiOverlayMs; }

  void SetLastFluidMapCpuMs(double ms) { LastFluidMapCpuMs = ms; }
  void SetLastFluidMapGpuMs(double ms) { LastFluidMapGpuMs = ms; }
  void SetLastFluidMapDirtyChunks(int n) { LastFluidMapDirtyChunks = n; }
  void SetLastFluidMapFullRebuild(bool v) { LastFluidMapFullRebuild = v; }
  double GetLastFluidMapCpuMs() const { return LastFluidMapCpuMs; }
  double GetLastFluidMapGpuMs() const { return LastFluidMapGpuMs; }
  int GetLastFluidMapDirtyChunks() const { return LastFluidMapDirtyChunks; }
  bool GetLastFluidMapFullRebuild() const { return LastFluidMapFullRebuild; }

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
    int genQueuePending{0};
    int genInFlight{0};
    int genBacklogTotal{0};
    double populateMsLast{0.0};
    double populateMsEma{0.0};
    double populateSampleMs{0.0};
    double populateTerrainMs{0.0};
    double populateCarveMs{0.0};
    double populatePostMs{0.0};
    bool asyncMeshingEnabled{true};
    int greedyCacheEntries{0};
    int framesSinceLoad{0};
    bool meshBacklogCleared{false};
    double physicsStepMs{0.0};
    double physicsMovementMs{0.0};
    double physicsBlockMs{0.0};
    double physicsDrainMs{0.0};
    double wallFrameMs{0.0};
    double swapWaitMs{0.0};
    double simMs{0.0};
    double unaccountedMs{0.0};
    int physicsSimulationSteps{0};
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
    int pendingPlayerRelights{0};
    int pendingBgRelights{0};
    int asyncRelightInflight{0};
    uint64_t relightDiscardedLate{0};
    uint64_t meshDiscardedLate{0};
    double relightCompletedPerSec{0.0};
  };

  const MovementDiagnostics &GetMovementDiagnostics() const
  {
    return MovementDiag;
  }

  void SetStreamingEnabled(bool enabled);
  void SetRenderDistanceChunks(int distance);
  int GetRenderDistanceChunks() const { return RenderDistanceChunks; }
  int GetEffectiveRenderDistance() const { return EffectiveRenderDistance; }
  /// Visual/effective RD + 1: promote, mesh focus, incomplete scan, recover.
  int GetStreamingFocusRadius() const
  {
    return std::max(1, EffectiveRenderDistance) + 1;
  }
  float GetEffectiveFogStartRatio() const { return EffectiveFogStartRatio; }
  float GetAltitudeAboveTerrain() const { return AltitudeAboveTerrain; }
  void SetAltitudeAboveTerrain(float altitude) { AltitudeAboveTerrain = altitude; }
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
  void TickEnvironment(float dtSeconds);
  const EnvironmentState &GetEnvironmentState() const
  {
    return EnvironmentStateData;
  }
  const LightingSettings &GetLightingSettings() const
  {
    return LightingSettingsData;
  }
  void SetTimeOfDayNormalized(float value);
  void AddTimeOfDayNormalized(float delta);
  void SetTimeFrozen(bool frozen) { EnvironmentStateData.TimeFrozen = frozen; }
  void SetDayLengthMinutes(float minutes);
  void SetWeather(WeatherType weather, float transitionSeconds = 45.0f);
  void SetWeatherByName(const std::string &name,
                        float transitionSeconds = 45.0f);
  void SetWeatherInternal(WeatherType weather, float transitionSeconds,
                          bool manual_override);
  void ApplyEnvironmentConfig(const EnvironmentConfig &config,
                              bool reset_weather_runtime = true);
  const EnvironmentConfig &GetEnvironmentConfig() const
  {
    return EnvironmentSettingsData;
  }
  EnvironmentConfig &GetEnvironmentConfigMutable()
  {
    return EnvironmentSettingsData;
  }
  float GetCelestialHorizonFade() const;
  void SetWeatherAutoEnabled(bool enabled);
  bool IsWeatherAutoEnabled() const;
  void ClearWeatherManualOverride();
  std::string GetWeatherAutoStatusText() const;
  std::string GetWeatherName() const;
  static std::string WeatherTypeToString(WeatherType value);
  static bool WeatherTypeFromString(const std::string &value, WeatherType &out);
  void SetLightingDebugEnabled(bool enabled)
  {
    LightingSettingsData.DebugEnabled = enabled;
    if (!enabled)
    {
      LightingSettingsData.DebugMode = 0;
    }
    else if (LightingSettingsData.DebugMode == 0)
    {
      LightingSettingsData.DebugMode = 1;
    }
  }
  void SetLightingDebugMode(uint8_t mode)
  {
    LightingSettingsData.DebugMode = mode;
    LightingSettingsData.DebugEnabled = mode != 0;
  }
  void SetLightingMinAmbient(float value)
  {
    LightingSettingsData.MinAmbient = std::clamp(value, 0.02f, 0.5f);
  }
  void SetWeatherOverlayEnabled(bool enabled)
  {
    LightingSettingsData.WeatherOverlayEnabled = enabled;
  }
  void SetWeatherParticlesEnabled(bool enabled)
  {
    LightingSettingsData.WeatherParticlesEnabled = enabled;
  }
  void SetWeatherDebugMode(uint8_t mode)
  {
    LightingSettingsData.WeatherDebugMode = mode;
  }
  void EnsureDefaultCelestialBodies();
  void RefreshSkyVisualStateForRender();
  void UpdateCelestialLightingFactors();
  void ResetCelestialBodies();
  void SetStarVisibility(float value);
  void SetCloudCoverage(float value);
  void RebuildAllLightingDirtyMeshes();

  void SetLightingRelightDeferred(bool deferred)
  {
    LightingRelightDeferred = deferred;
  }
  bool IsLightingRelightDeferred() const { return LightingRelightDeferred; }
  /// Idle lit-but-dirty catch-up: MarkRelit dirties primary only (no seam flood).
  void SetSuppressRelightSeamDirty(bool suppress)
  {
    SuppressRelightSeamDirty = suppress;
  }
  bool IsSuppressRelightSeamDirty() const { return SuppressRelightSeamDirty; }
  bool ShouldDeferStreamingMeshForRelight() const
  {
    return ProceduralTemplate.AsyncRelight && !LightingRelightDeferred;
  }
  void SetLightingSkylightBulkComplete(bool complete)
  {
    LightingSkylightBulkComplete = complete;
  }
  void SetCooperativeBulkGenerating(bool value)
  {
    CooperativeBulkGenerating = value;
  }
  bool IsCooperativeBulkGenerating() const { return CooperativeBulkGenerating; }
  void SetLastMovementFrameMs(double ms) { LastMovementFrameMs = ms; }
  double GetLastMovementFrameMs() const { return LastMovementFrameMs; }
  float GetLastMovementSpeed() const { return LastMovementSpeed; }
  /// Horizontal motion/view direction for mesh/relight forward bias (xz).
  glm::vec2 GetLastMovementDirXz() const { return LastMovementDirXz; }
  void RelightTerrainColumn(int world_x, int world_z, int min_y, int max_y,
                            bool priority_mesh = false,
                            bool include_skylight = true,
                            bool include_block_light = true);
  void RelightPlayerEdit(const std::vector<glm::ivec3> &block_positions,
                         int min_world_y);
  void EnqueueAsyncTerrainColumnRelight(int world_x, int world_z, int min_y,
                                        int max_y, bool include_skylight = true,
                                        bool include_block_light = true);
  void EnqueueAsyncChunkSkylightRelight(glm::ivec3 chunk_coord,
                                        int frontier_iterations = 1);
  void EnqueueAsyncChunkRelight(glm::ivec3 chunk_coord, bool include_skylight,
                                bool include_block_light,
                                int frontier_iterations);
  int DrainAsyncRelightResults(int max_per_frame, bool priority_mesh,
                               bool enqueue_background_frontier);
  bool HasPendingAsyncRelightWork() const;
  int GetAsyncRelightInFlightCount() const;
  bool IsAsyncRelightColumnInFlight(glm::ivec2 ground_xz) const;
  /// Drop column inflight marks when the async builder has no jobs (stale set).
  void ReconcileAsyncRelightColumnInFlight();
  uint64_t GetRelightDiscardedLateCount() const;
  uint64_t GetMeshDiscardedLateCount() const;
  int GetPlayerRelightMeshBurstFrames() const
  {
    return PlayerRelightMeshBurstFrames;
  }
  void TickPlayerRelightMeshBurst();
  void FlushPendingRelightMeshColumns(int max_columns_per_flush = 8);
  /// Re-enqueue skylight for focus columns that already have a mesh but no sky.
  int RecoverUnlitFocusMeshes(int max_columns = 4);
  /// Idle stop-recovery: sync relight+remesh for focus columns stuck with
  /// GreedyMesh while PendingLight (async relight deadlock).
  int RecoverStickyBlackFocusSync(int max_columns = 1);
  /// Idle hover: re-dirty LitReady focus columns that already have GreedyMesh
  /// (MarkRelit cleared PendingLight but async never remeshed — dark/stale).
  int RefreshIdleFocusGreedyRemesh(int max_columns = 4);
  /// Idle stop: sync rebuild nearest focus column when async pool saturated.
  int SyncIdleFocusGreedyRemesh(int max_columns = 1);
  /// Clear PendingLight after mesh committed for lit focus columns.
  int ClearPendingLightAfterMeshCommitted(int max_columns = 8);
  /// Drop StickyRemeshAfterLight columns outside radius (cruise prune).
  int PruneStickyRemeshOutside(glm::ivec3 focus_ground_chunk, int radius_chunks);
  /// Focus ingress: Dirty + priority relight for Lighting columns without mesh.
  int AdmitFocusMeshIngress(int max_columns = 8);
  /// Idle: Dirty for focus Lighting/Pending columns that never got a Dirty mark.
  int AdmitFocusLightingWithoutDirty(int max_columns = 8);
  /// Ring-scan focus for solid slices missing GreedyCache; mark Dirty only.
  int AdmitFocusVisibleMissing(int max_columns = 8,
                               glm::vec2 forward_xz = glm::vec2(0.0f));

  /// Near-focus columns waiting for first light before first mesh (plan A).
  void NotePendingLightBeforeMesh(glm::ivec3 ground, int min_y, int max_y);
  void ClearPendingLightBeforeMesh(glm::ivec2 ground_xz);
  bool IsPendingLightBeforeMesh(glm::ivec2 ground_xz) const;
  bool HasPendingLightBeforeMeshNear(glm::ivec3 focus_ground_horiz,
                                     int radius_chunks) const;
  size_t GetPendingLightBeforeMeshCount() const
  {
    return PendingLightBeforeMesh.size();
  }
  int CountPendingLightBeforeMeshNear(glm::ivec3 focus_ground_horiz,
                                      int radius_chunks) const;
  /// "(cx,cz),..." for PendingLightBeforeMesh inside focus (max_cols cap).
  std::string FormatPendingLightFocusColumns(glm::ivec3 focus_ground_horiz,
                                             int radius_chunks,
                                             int max_cols = 12) const;
  /// Focus columns with GreedyMesh and PendingLightBeforeMesh (sticky black).
  int CountBlackStickyFocusMeshes(glm::ivec3 focus_ground_chunk,
                                  int radius_chunks) const;
  /// PendingLight columns that already have a greedy mesh (dark preview).
  int CountPendingDarkFocusMeshes(glm::ivec3 focus_ground_chunk,
                                  int radius_chunks) const;
  /// Re-queue priority relight for PendingLightBeforeMesh columns under focus.
  int PromotePendingLightRelightsNear(glm::ivec3 focus_ground_horiz,
                                      int radius_chunks);
  void PromotePendingLightBeforeMesh(const std::vector<glm::ivec3> &relit_chunks,
                                     bool priority_mesh);

  void SetColumnEmergeState(glm::ivec3 ground, ColumnEmergeState state);
  ColumnEmergeState GetColumnEmergeState(glm::ivec3 ground) const;
  void ClearColumnEmergeState(glm::ivec2 ground_xz);
  /// True when column has left the light gate (LitReady / Meshing / RenderReady).
  bool IsColumnLitReady(glm::ivec3 ground) const;
  /// True when column may unlock outer streaming rings (LitReady+).
  bool IsColumnVisualReadyForRing(glm::ivec3 ground) const;
  /// Strict visible contract: no pending light, stable mesh, render-safe column.
  bool IsColumnRenderReady(glm::ivec3 ground) const;
  /// Focus columns that are loaded but not yet safe to render.
  int CountUnfinishedVisualNear(glm::ivec3 focus_ground_chunk,
                                int radius_chunks) const;
  /// Split unfinished focus by movement/view forward (dot>=0 ahead, else behind).
  void CountUnfinishedVisualByFacing(glm::ivec3 focus_ground_chunk,
                                     int radius_chunks, glm::vec2 forward_xz,
                                     int &out_ahead, int &out_behind) const;
  /// Authoritative mesh gate: LitReady+ or underfeet first-mesh preview.
  bool MayMeshColumn(glm::ivec3 ground, bool underfeet_preview) const;
  /// Centralized focus-ring drain: promote pending relight + clear lit-ready
  /// pending after mesh. Does not Recover/Admit (those stay watchdogs).
  int DrainFocusVisualWork(glm::ivec3 focus_ground_horiz, int radius_chunks,
                           int clear_pending_budget);
  /// Alias of DrainFocusVisualWork (legacy call sites).
  int DrainColumnWork(glm::ivec3 focus_ground_horiz, int radius_chunks,
                      int clear_pending_budget);
  /// Idle sync relight for focus pending columns that already have preview mesh.
  int DrainIdleFocusPendingLight(glm::ivec3 focus_ground_horiz,
                                 int radius_chunks, int max_columns);
  /// Same as above but sync RelightTerrainColumn (strict budget; outer ring first).
  int DrainIdleFocusPendingLightSync(glm::ivec3 focus_ground_horiz,
                                     int radius_chunks, int max_columns);
  /// Commit-time skylight seed is only safe when the neighbor ring is loaded.
  bool CanSeedSkylightAtCommit(glm::ivec3 ground) const;

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

  friend class UWorldViewBinding;
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
  void DrainAsyncRelightResults();
  void TickMeshEmerge();
  void ApplyUserToCamera(const std::shared_ptr<UUser> &user);
  bool IsReasonablePlayerPosition(const glm::vec3 &position) const;
  void SanitizeUserPosition(const std::shared_ptr<UUser> &user);
  // Phase 6 fluid facade slice: isolate placement/break flood policy.
  bool TryAddFluidObject(glm::ivec3 blockPos, BlockId liquidId);
  void ApplyBreakSiteFluidFlood(glm::ivec3 blockPos,
                                std::vector<glm::ivec3> &mesh_touch_blocks);
  void ApplyEditLighting(const std::vector<glm::ivec3> &block_positions);
  void ApplyEditFastRelight(const std::vector<glm::ivec3> &block_positions);
  void EnqueueAsyncPlayerRelight(const std::vector<glm::ivec3> &block_positions,
                                 int min_world_y);
  void EnsureAsyncRelightBuilder();
  void CancelAsyncRelightWork();
  /// primary_grounds: column XZ whose light job completed — only these clear
  /// PendingLightBeforeMesh. Neighbors in relit_chunks remesh for seams only.
  void MarkRelitChunksForMesh(const std::vector<glm::ivec3> &relit_chunks,
                              bool priority_mesh,
                              const std::vector<glm::ivec2> &primary_grounds);
  void AccumulateRelightMeshColumns(
      const std::vector<glm::ivec3> &relit_chunks);
  void EnsurePlayerOnGround();
  void MarkBlockChunkDirty(glm::ivec3 blockPos);
  void
  MarkBlocksChunkDirtyBatch(const std::vector<glm::ivec3> &block_positions,
                            bool sync_neighbor_chunks = false);
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
  void MarkTerrainChunkMeshDirtySeamed(glm::ivec3 groundChunkCoord, int min_y,
                                       int max_y,
                                       bool include_horizontal_neighbors = true);
  void SaveMovementDiagnostics(const std::string &file_name) const;
  void ResetMeshLoadDiagnostics();
  void TickMeshLoadDiagnostics();
  void ApplyCelestialBodiesFromConfig();
  void SyncDefaultCelestialBodiesToConfig();
  void TickWeatherAuto(float dtSeconds);

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
  std::unique_ptr<UWorldViewBinding> ViewBinding;
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
  std::unique_ptr<UAsyncRelightBuilder> AsyncRelight;
  uint64_t NextAsyncRelightJobId{1};
  bool StepUpEnabled{true};
  bool FoliageClimbEnabled{true};
  RenderSettings Render;
  EnvironmentState EnvironmentStateData;
  EnvironmentConfig EnvironmentSettingsData;
  LightingSettings LightingSettingsData;
  bool LightingRelightDeferred{false};
  bool SuppressRelightSeamDirty{false};
  bool LightingSkylightBulkComplete{false};
  bool CooperativeBulkGenerating{false};
  double LastMovementFrameMs{0.0};
  int PlayerRelightMeshBurstFrames{0};
  struct PendingRelightMeshColumnRange
  {
    int min_y{0};
    int max_y{0};
  };
  struct GroundColumnHash
  {
    std::size_t operator()(const glm::ivec2 &v) const noexcept
    {
      return std::hash<int64_t>{}((static_cast<int64_t>(v.x) << 32) ^
                                  static_cast<uint32_t>(v.y));
    }
  };
  std::unordered_map<glm::ivec2, PendingRelightMeshColumnRange, GroundColumnHash>
      PendingRelightMeshColumns;
  /// Near columns: light must apply before first mesh dirty.
  std::unordered_map<glm::ivec2, PendingRelightMeshColumnRange, GroundColumnHash>
      PendingLightBeforeMesh;
  /// Lit columns awaiting remesh after MarkRelit (GreedyMesh present, stale).
  std::unordered_set<glm::ivec2, GroundColumnHash> StickyRemeshAfterLight;
  /// Columns with an async terrain-column relight job in flight (chunk xz).
  std::unordered_set<glm::ivec2, GroundColumnHash> AsyncRelightColumnsInFlight;
  std::unordered_map<glm::ivec2, ColumnEmergeState, GroundColumnHash>
      ColumnEmergeStates;
  bool SpawnAreaPreparedByCooperativeLoad{false};
  bool ShutdownPrepared{false};
  bool BackgroundQuiesceFinished{false};
  void PrepareForShutdownWithBudgets(std::chrono::milliseconds quiesce_budget,
                                     std::chrono::milliseconds abandon_budget,
                                     std::chrono::milliseconds mesh_idle_budget);
  int RenderDistanceChunks{4};
  int EffectiveRenderDistance{4};
  float EffectiveFogStartRatio{0.85f};
  float AltitudeAboveTerrain{0.0f};
  StreamingAltitudePolicyParams AltitudeParams;
  glm::vec3 LastCameraPosition{0.0f};
  float LastMovementSpeed{0.0f};
  glm::vec2 LastMovementDirXz{0.0f};
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
  uint64_t DurationViewUpdateMks{0};
  double LastSwapWaitMs{0.0};
  double LastInputMs{0.0};
  double LastAppUpdateMs{0.0};
  double LastWorldTickMs{0.0};
  double LastPrepareFrameMs{0.0};
  double LastPostSceneMs{0.0};
  double LastGuiOverlayMs{0.0};
  double LastFluidMapCpuMs{0.0};
  double LastFluidMapGpuMs{0.0};
  int LastFluidMapDirtyChunks{0};
  bool LastFluidMapFullRebuild{false};
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
  double WallFrameDeltaSec{0.0};
  uint64_t PhysicsEventOrderCounter{0};
  std::unique_ptr<class UWorldBlockPhysicsService> BlockPhysicsService;
  std::unique_ptr<class UWorldMovementPhysicsService> MovementPhysicsService;
  std::unique_ptr<class UWorldChunkDirtyService> ChunkDirtyService;
  std::unique_ptr<IUPhysicsScheduler> PhysicsScheduler;

  friend class UWorldChunkDirtyService;
  friend class UWorldFluidFacade;
  friend class UWorldCreatureFacade;
  friend class UWorldBlockPhysicsService;
};

} // namespace cutum

#endif // WORLD_H
