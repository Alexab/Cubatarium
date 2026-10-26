#ifndef WORLDENVIRONMENT_H
#define WORLDENVIRONMENT_H

#include "Activity/CreatureActivityDirector.h"
#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Pose/CreaturePosePresenterRegistry.h"
#include "Creatures/Spawning/CreaturePopulationRules.h"
#include "Creatures/Spawning/UCreatureSpawnPlanner.h"
#include "World/Environment/CreatureSpatialIndex.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureDefinitionStorage;
class USkinDefinitionStorage;
struct CreatureDefinition;
class UWorld;
class UUser;
class UCamera;
struct ResolvedCreatureAppearance;

class UWorldEnvironment
{
public:
  explicit UWorldEnvironment(UWorld &owner);

  void Initialize();

  void SetCreatureDefinitionStorage(
      std::shared_ptr<UCreatureDefinitionStorage> storage);
  void SetSkinDefinitionStorage(std::shared_ptr<USkinDefinitionStorage> storage);
  const std::shared_ptr<UCreatureDefinitionStorage> &
  GetCreatureDefinitionStorage() const
  {
    return CreatureDefinitions;
  }
  const std::shared_ptr<USkinDefinitionStorage> &GetSkinDefinitionStorage() const
  {
    return SkinDefinitions;
  }

  UCreature *GetCreature(CreatureId id);
  const UCreature *GetCreature(CreatureId id) const;
  UCreature *GetControlledCreature();
  const UCreature *GetControlledCreature() const;
  UCreature *GetPlayerCreature();
  const UCreature *GetPlayerCreature() const;

  CreatureId GetControlledCreatureId() const { return ControlledCreatureId; }
  CreatureId GetPlayerCreatureId() const { return PlayerCreatureId; }
  void SetPlayerCreatureId(CreatureId id) { PlayerCreatureId = id; }
  void SetControlledCreatureId(CreatureId id) { ControlledCreatureId = id; }

  bool SetControlledCreature(CreatureId id);
  void RegisterDefaultActivityAgents();
  void SnapCreatureFeetToGround(UCreature &creature) const;

  std::optional<ControlledCreatureInfo> QueryControlledCreatureInfo() const;
  std::vector<CreatureId> CreaturesInRadius(const glm::vec3 &center,
                                            float radius) const;
  std::vector<CreatureNeighborView>
  QueryCreatureNeighborsInRadius(const glm::vec3 &center, float radius,
                                 CreatureId skip_id) const;

  CreatureId SpawnCreature(const std::string &speciesId,
                           const glm::vec3 &bodyOrigin,
                           const std::string &skinId = "",
                           CreatureSpawnSource source =
                               CreatureSpawnSource::CreativeManual);
  bool SpawnCreatureByView(const std::string &speciesId);
  bool CanSpawnCreatureByView(const std::string &speciesId);
  std::string GetCreatureSpawnBlockedHint(const std::string &speciesId);
  std::optional<CreatureId> PickCreatureByView(const glm::vec3 &eye,
                                               const glm::vec3 &front,
                                               float maxDistance) const;
  bool TryApplySkin(CreatureId target, const std::string &skinId,
                    std::string *outError = nullptr);

  void RemoveCreature(CreatureId id);
  void ForEachCreature(const std::function<void(UCreature &)> &fn);
  void ForEachCreature(
      const std::function<void(const UCreature &)> &fn) const;

  void ClearCreatures();
  void ResetBeforeEntityLoad();

  void LinkUsersToPlayerCreatures(
      std::map<std::string, std::shared_ptr<UUser>> &users);

  void LoadCreatures(const std::string &file_name);
  void SaveCreatures(const std::string &file_name);
  void ReloadAllCreatureVisuals();

  void TickActivity(class IUWorldPerception &perception,
                    class UWorldCreatureActivitySink &sink, float dt,
                    bool stress_tick = false);
  void SyncCreatureSpatialIndex();
  /// Per-move spatial index maintenance (skips player characters).
  void UpdateCreatureInSpatialIndex(const UCreature &creature);

  CreaturePopulationCounts CountNaturalPopulation(
      const glm::vec3 &playerOrigin);
  void TryNaturalPopulationSpawn(
      const glm::vec3 &playerOrigin, const CreaturePopulationCounts &counts,
      const std::vector<WeightedSpawnOption<const CreatureDefinition *>>
          &eligible);
  void TickNaturalPopulation(float dt);
  void ValidateLoadedCreaturePositions();
  int GetLastActivityAgentsTicked() const
  {
    return ActivityDirector.GetLastAgentsTicked();
  }
  int GetLastActivityAgentsDeferred() const
  {
    return ActivityDirector.GetLastAgentsDeferred();
  }

  bool CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                    CreatureId skipCreatureId) const;

  std::string ResolveAnimationTypeId(const UCreature &creature) const;
  const CreatureDefinition *GetCreatureDefinition(const std::string &typeId) const;
  ResolvedCreatureAppearance
  GetResolvedAppearance(const UCreature &creature) const;

  UCreatureActivityDirector &GetActivityDirector() { return ActivityDirector; }
  const UCreatureActivityDirector &GetActivityDirector() const
  {
    return ActivityDirector;
  }
  UCreaturePosePresenterRegistry &GetPosePresenterRegistry()
  {
    return PosePresenterRegistry;
  }
  const UCreaturePosePresenterRegistry &GetPosePresenterRegistry() const
  {
    return PosePresenterRegistry;
  }

private:

  UWorld &Owner;

  std::unordered_map<CreatureId, std::unique_ptr<UCreature>> Creatures;
  CreatureId NextCreatureId{1};
  CreatureId PlayerCreatureId{0};
  CreatureId ControlledCreatureId{0};
  std::shared_ptr<UCreatureDefinitionStorage> CreatureDefinitions;
  std::shared_ptr<USkinDefinitionStorage> SkinDefinitions;
  UCreatureActivityDirector ActivityDirector;
  UCreaturePosePresenterRegistry PosePresenterRegistry;
  UCreatureSpatialIndex CreatureSpatialIndex;
  bool CreatureSpatialIndexReady{false};
  float NaturalPopulationTimer{0.f};
  float LoadedCreaturePositionValidationTimer{0.f};
  bool LoadedCreaturePositionValidationPending{true};
  std::mt19937 NaturalPopulationRng{0xC0BA7AULL};
  UCreatureSpawnPlanner SpawnPlanner;

  // Resolved-appearance cache keyed "speciesskin"; validated against
  // storage revisions/pointers so any reload invalidates it (audit V6).
  mutable std::unordered_map<std::string, ResolvedCreatureAppearance>
      CreatureAppearanceCache;
  mutable uint64_t AppearanceCacheDefsRevision{0};
  mutable uint64_t AppearanceCacheSkinsRevision{0};
  mutable const UCreatureDefinitionStorage *AppearanceCacheDefsPtr{nullptr};
  mutable const USkinDefinitionStorage *AppearanceCacheSkinsPtr{nullptr};
};

/// Body origin ~3 blocks ahead of the viewer camera (falls back to the world
/// spawn point / player body). Shared by palette spawn, QA and /spawn.
glm::vec3 ComputeSpawnBodyOriginAhead(UWorld &world);

} // namespace cutum

#endif // WORLDENVIRONMENT_H
