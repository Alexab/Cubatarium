#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Render/Camera/Camera.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Core/World.h"
#include "World/Core/WorldCreatureFacade.h"

namespace cutum
{

void UWorld::SetCreatureDefinitionStorage(
    std::shared_ptr<UCreatureDefinitionStorage> storage)
{
  Environment.SetCreatureDefinitionStorage(std::move(storage));
}

void UWorld::RegisterDefaultActivityAgents()
{
  Environment.RegisterDefaultActivityAgents();
}

std::optional<ControlledCreatureInfo>
UWorld::QueryControlledCreatureInfo() const
{
  return Environment.QueryControlledCreatureInfo();
}

std::vector<CreatureId> UWorld::CreaturesInRadius(const glm::vec3 &center,
                                                  float radius) const
{
  return Environment.CreaturesInRadius(center, radius);
}

std::vector<CreatureNeighborView> UWorld::QueryCreatureNeighborsInRadius(
    const glm::vec3 &center, float radius, CreatureId skip_id) const
{
  return Environment.QueryCreatureNeighborsInRadius(center, radius, skip_id);
}

bool UWorld::CreatureVolumeClearAt(const glm::vec3 &body_origin,
                                   const glm::vec3 &size_blocks,
                                   CreatureId skip_id) const
{
  const CollisionVolume vol = CollisionVolumeFromBody(body_origin, size_blocks);
  return !CheckCreatureCollisionVolume(vol, skip_id);
}

std::optional<glm::vec3> UWorld::GetCreatureBodyOrigin(CreatureId id) const
{
  const UCreature *creature = GetCreature(id);
  if (!creature)
  {
    return std::nullopt;
  }
  return creature->GetBodyOrigin();
}

bool UWorld::CanCreatureOccupyAt(CreatureHabitat habitat,
                               const glm::vec3 &bodyOrigin,
                               const glm::vec3 &sizeBlocks) const
{
  return cutum::CanCreatureOccupyAt(*this, habitat, bodyOrigin, sizeBlocks);
}

bool UWorld::HabitatAllowsAt(CreatureHabitat habitat,
                             const glm::vec3 &bodyOrigin,
                             const glm::vec3 &sizeBlocks) const
{
  return cutum::HabitatAllowsAt(*this, habitat, bodyOrigin, sizeBlocks);
}

bool UWorld::HabitatAllowsMovementAt(CreatureHabitat habitat,
                                     const glm::vec3 &bodyOrigin,
                                     const glm::vec3 &sizeBlocks) const
{
  return cutum::HabitatAllowsMovementAt(*this, habitat, bodyOrigin, sizeBlocks);
}

bool UWorld::IsWithinActivityRange(const glm::vec3 &body_origin) const
{
  const std::optional<ControlledCreatureInfo> controlled =
      QueryControlledCreatureInfo();
  if (controlled)
  {
    const glm::vec3 delta = body_origin - controlled->eyePosition;
    const float dist_sq = delta.x * delta.x + delta.z * delta.z;
    const float activity_radius =
        static_cast<float>(GetEffectiveRenderDistance() * CHUNK_SIZE) + 16.0f;
    if (dist_sq <= activity_radius * activity_radius)
    {
      return true;
    }
  }
  if (!Streaming || !Streaming->HasStreamer() ||
      !Streaming->IsStreamingEnabled())
  {
    return true;
  }
  const UCreature *controlled_creature = Environment.GetControlledCreature();
  const auto camera = GetCurrentUserCamera();
  if (!controlled_creature || !camera)
  {
    return true;
  }
  const glm::vec3 eyePos = camera->GetPosition();
  float feetY =
      FeetYFromEye(eyePos, controlled_creature->GetEyeOffset().y);
  if (!camera->GetFreeMove() && camera->HasAnchoredFeet())
  {
    feetY = BoundsFeetY(controlled_creature->GetBodyOrigin());
  }
  const glm::ivec3 feetBlock =
      WorldPosToBlock(glm::vec3(eyePos.x, feetY + 0.01f, eyePos.z));
  const PlayerCapsule cap = camera->GetPlayerCapsule();
  return Streaming->GetStreamer()->IsPositionInActiveRing(
      body_origin, feetBlock, eyePos, cap);
}

void UWorld::SetActivityTickHz(float hz)
{
  const float clamped_hz = std::max(hz, 1.0f);
  Environment.GetActivityDirector().SetActivityTickInterval(1.0f / clamped_hz);
}

void UWorld::ClearCreaturesAndUsers()
{
  Environment.ClearCreatures();
  CurrentUserName.clear();
  Users.clear();
}

void UWorld::SetSkinDefinitionStorage(
    std::shared_ptr<USkinDefinitionStorage> storage)
{
  Environment.SetSkinDefinitionStorage(std::move(storage));
}

UCreature *UWorld::GetCreature(CreatureId id)
{
  return Environment.GetCreature(id);
}

const UCreature *UWorld::GetCreature(CreatureId id) const
{
  return Environment.GetCreature(id);
}

UCreature *UWorld::GetControlledCreature()
{
  return Environment.GetControlledCreature();
}

const UCreature *UWorld::GetControlledCreature() const
{
  return Environment.GetControlledCreature();
}

UCreature *UWorld::GetPlayerCreature() { return Environment.GetPlayerCreature(); }

const UCreature *UWorld::GetPlayerCreature() const
{
  return Environment.GetPlayerCreature();
}

bool UWorld::SetControlledCreature(CreatureId id)
{
  return Environment.SetControlledCreature(id);
}

void UWorld::ApplyLocomotionDefinitionToCamera(UCamera &camera,
                                               const CreatureDefinition &def) const
{
  camera.ApplyCreatureLocomotion(def.locomotion, def.bounds, def.eyeHeight);
}

void UWorld::SnapCreatureFeetToGround(UCreature &creature) const
{
  Environment.SnapCreatureFeetToGround(creature);
}

CreatureId UWorld::SpawnCreature(const std::string &speciesId,
                                 const glm::vec3 &bodyOrigin,
                                 const std::string &skinId)
{
  return Environment.SpawnCreature(speciesId, bodyOrigin, skinId);
}

void UWorld::RemoveCreature(CreatureId id) { Environment.RemoveCreature(id); }

void UWorld::ForEachCreature(const std::function<void(UCreature &)> &fn)
{
  Environment.ForEachCreature(fn);
}

void UWorld::ForEachCreature(
    const std::function<void(const UCreature &)> &fn) const
{
  Environment.ForEachCreature(fn);
}

std::string UWorld::ResolveAnimationTypeId(const UCreature &creature) const
{
  return Environment.ResolveAnimationTypeId(creature);
}

const CreatureDefinition *
UWorld::GetCreatureDefinition(const std::string &typeId) const
{
  return Environment.GetCreatureDefinition(typeId);
}

ResolvedCreatureAppearance
UWorld::GetResolvedAppearance(const UCreature &creature) const
{
  return Environment.GetResolvedAppearance(creature);
}

bool UWorld::SpawnCreatureByView(const std::string &speciesId)
{
  return UWorldCreatureFacade::SpawnCreatureByView(*this, speciesId);
}

bool UWorld::CanSpawnCreatureByView(const std::string &speciesId)
{
  return UWorldCreatureFacade::CanSpawnCreatureByView(*this, speciesId);
}

std::string UWorld::GetCreatureSpawnBlockedHint(const std::string &speciesId)
{
  return UWorldCreatureFacade::GetCreatureSpawnBlockedHint(*this, speciesId);
}

std::optional<CreatureId> UWorld::PickCreatureByView(const glm::vec3 &eye,
                                                     const glm::vec3 &front,
                                                     float maxDistance) const
{
  return Environment.PickCreatureByView(eye, front, maxDistance);
}

bool UWorld::TryApplySkin(CreatureId target, const std::string &skinId,
                          std::string *outError)
{
  return Environment.TryApplySkin(target, skinId, outError);
}

void UWorld::LinkUsersToPlayerCreatures()
{
  Environment.LinkUsersToPlayerCreatures(Users);
}

void UWorld::ResetCreaturesBeforeEntityLoad()
{
  Environment.ResetBeforeEntityLoad();
}

void UWorld::LoadCreatures(const std::string &file_name)
{
  Environment.LoadCreatures(file_name);
}

void UWorld::SaveCreatures(const std::string &file_name)
{
  Environment.SaveCreatures(file_name);
}

} // namespace cutum
