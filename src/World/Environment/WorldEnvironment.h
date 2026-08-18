#ifndef WORLDENVIRONMENT_H
#define WORLDENVIRONMENT_H

#include "Activity/CreatureActivityDirector.h"
#include "Activity/CreatureActivityTypes.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Pose/CreaturePosePresenterRegistry.h"
#include "World/Environment/CreatureSpatialIndex.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
                           const std::string &skinId = "");
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
                    class UWorldCreatureActivitySink &sink, float dt);
  void SyncCreatureSpatialIndex();

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
};

} // namespace cutum

#endif // WORLDENVIRONMENT_H
