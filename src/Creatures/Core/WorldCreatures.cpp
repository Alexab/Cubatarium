#include "Activity/CreatureActivityRegistry.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

void UWorld::SetCreatureDefinitionStorage(
    std::shared_ptr<UCreatureDefinitionStorage> storage)
{
  CreatureDefinitions = std::move(storage);
  RegisterDefaultActivityAgents();
}

void UWorld::RegisterDefaultActivityAgents()
{
  std::vector<std::pair<CreatureId, std::string>> rebind;
  for (const auto &entry : Creatures)
  {
    if (!entry.second)
    {
      continue;
    }
    const CreatureDefinition *def =
        GetCreatureDefinition(entry.second->GetTypeId());
    if (!def || def->role == CreatureRole::ControlledDefault ||
        def->behavior.Id.empty() || def->behavior.Id == "none")
    {
      continue;
    }
    rebind.emplace_back(entry.first, def->behavior.Id);
  }
  ActivityDirector.Clear();
  RegisterDefaultCreatureActivityAgents(ActivityDirector);
  for (const auto &pair : rebind)
  {
    ActivityDirector.OnCreatureAdded(pair.first, pair.second);
  }
}

std::optional<ControlledCreatureInfo>
UWorld::QueryControlledCreatureInfo() const
{
  if (ControlledCreatureId == 0)
  {
    return std::nullopt;
  }
  const UCreature *controlled = GetControlledCreature();
  if (!controlled)
  {
    return std::nullopt;
  }
  ControlledCreatureInfo info;
  info.Id = ControlledCreatureId;
  info.eyePosition = controlled->GetEyePosition();
  return info;
}

std::vector<CreatureId> UWorld::CreaturesInRadius(const glm::vec3 &center,
                                                  float radius) const
{
  std::vector<CreatureId> out;
  const float radiusSq = radius * radius;
  ForEachCreature(
      [&](const UCreature &creature)
      {
        if (creature.IsPlayerCharacter())
        {
          return;
        }
        const glm::vec3 delta = creature.GetBodyOrigin() - center;
        const float distSq = delta.x * delta.x + delta.z * delta.z;
        if (distSq <= radiusSq)
        {
          out.push_back(creature.GetId());
        }
      });
  return out;
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

void UWorld::ClearCreaturesAndUsers()
{
  ActivityDirector.Clear();
  Creatures.clear();
  NextCreatureId = 1;
  PlayerCreatureId = 0;
  ControlledCreatureId = 0;
  CurrentUserName.clear();
  Users.clear();
}

void UWorld::SetSkinDefinitionStorage(
    std::shared_ptr<USkinDefinitionStorage> storage)
{
  SkinDefinitions = std::move(storage);
}

UCreature *UWorld::GetCreature(CreatureId Id)
{
  const auto it = Creatures.find(Id);
  return it != Creatures.end() ? it->second.get() : nullptr;
}

const UCreature *UWorld::GetCreature(CreatureId Id) const
{
  const auto it = Creatures.find(Id);
  return it != Creatures.end() ? it->second.get() : nullptr;
}

UCreature *UWorld::GetControlledCreature()
{
  return GetCreature(ControlledCreatureId);
}

const UCreature *UWorld::GetControlledCreature() const
{
  return GetCreature(ControlledCreatureId);
}

UCreature *UWorld::GetPlayerCreature() { return GetCreature(PlayerCreatureId); }

const UCreature *UWorld::GetPlayerCreature() const
{
  return GetCreature(PlayerCreatureId);
}

bool UWorld::SetControlledCreature(CreatureId Id)
{
  if (Id == 0 || !GetCreature(Id))
  {
    return false;
  }
  if (ControlledCreatureId != 0)
  {
    if (UCreature *prev = GetCreature(ControlledCreatureId))
    {
      prev->SetPossessed(false);
    }
  }
  ControlledCreatureId = Id;
  if (UCreature *c = GetCreature(Id))
  {
    c->SetPossessed(true);
    if (const CreatureDefinition *def = GetCreatureDefinition(c->GetTypeId()))
    {
      if (auto camera = GetCurrentUserCamera())
      {
        ApplyLocomotionDefinitionToCamera(*camera, *def);
      }
    }
  }
  return true;
}

void UWorld::ApplyLocomotionDefinitionToCamera(
    UCamera &camera, const CreatureDefinition &def) const
{
  camera.ApplyCreatureLocomotion(def.locomotion, def.bounds, def.eyeHeight);
}

void UWorld::SnapCreatureFeetToGround(UCreature &creature) const
{
  const glm::vec3 origin = creature.GetBodyOrigin();
  const int gx = WorldCoordToBlockIndex(origin.x);
  const int gz = WorldCoordToBlockIndex(origin.z);
  if (const std::optional<float> feetY = QueryGroundFeetYColumn(gx, gz))
  {
    creature.SetBodyOrigin(glm::vec3(origin.x, *feetY, origin.z));
  }
}

CreatureId UWorld::SpawnCreature(const std::string &speciesId,
                                 const glm::vec3 &bodyOrigin,
                                 const std::string &skinId)
{
  const CreatureDefinition *def =
      CreatureDefinitions ? CreatureDefinitions->Get(speciesId) : nullptr;
  if (!def)
  {
    std::cerr << "SpawnCreature: unknown species '" << speciesId << "'"
              << std::endl;
    return 0;
  }

  const glm::vec3 spawnOrigin =
      AdjustSpawnBodyOrigin(*this, *def, bodyOrigin);
  const glm::vec3 spawnSize = def->bounds.restSizeBlocks;
  if (def->role != CreatureRole::ControlledDefault)
  {
    if (!cutum::HabitatAllowsAtForSpawn(*this, def->habitat, spawnOrigin,
                                        spawnSize))
    {
      std::cerr << "SpawnCreature: invalid habitat for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
    const CollisionVolume vol = CollisionVolumeFromBody(spawnOrigin, spawnSize);
    if (CheckCreatureCollisionVolume(vol, 0) || CheckBlockCollisionVolume(vol))
    {
      std::cerr << "SpawnCreature: no space for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
  }

  const CreatureId Id = NextCreatureId++;
  const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);

  std::unique_ptr<UCreature> creature;
  if (def->role == CreatureRole::ControlledDefault)
  {
    creature = std::make_unique<UPlayer>(Id, speciesId, spawnOrigin);
  }
  else
  {
    creature =
        std::make_unique<UCreature>(Id, speciesId, spawnOrigin, eyeOffset);
  }

  creature->GetBoundsMutable().profile = def->bounds;
  if (def->bounds.restSizeBlocks.x > 0.0f)
  {
    creature->GetBoundsMutable().currentSizeBlocks = def->bounds.restSizeBlocks;
  }
  creature->SetCapabilities(def->locomotion);
  creature->SetLocomotionArchetype(def->locomotionArchetype);
  creature->SetModelYawOffsetDeg(def->visual.modelYawOffsetDeg);
  creature->SetWalkCycleHz(def->visual.Animation.walkCycleHz);
  creature->GetLocomotion().SetCollisionProfile(
      creature->GetBounds().currentSizeBlocks, def->eyeHeight);
  if (def->habitat != CreatureHabitat::Terrestrial)
  {
    creature->GetLocomotion().SetMode(CreatureMovementMode::Flying);
  }
  if (!skinId.empty())
  {
    creature->SetSkinId(skinId);
  }
  creature->SetVisual(CreateCreatureVisual(*def));
  Creatures[Id] = std::move(creature);
  if (def->habitat == CreatureHabitat::Terrestrial)
  {
    SnapCreatureFeetToGround(*Creatures[Id]);
  }
  else if (def->habitat == CreatureHabitat::Amphibious)
  {
    const EnvironmentSample env = ProbeEnvironmentAt(
        *this, Creatures[Id]->GetBodyOrigin(),
        def->bounds.restSizeBlocks);
    if (!env.inWater)
    {
      SnapCreatureFeetToGround(*Creatures[Id]);
    }
  }
  if (def->role != CreatureRole::ControlledDefault)
  {
    ActivityDirector.OnCreatureAdded(Id, def->behavior.Id);
  }
  return Id;
}

void UWorld::RemoveCreature(CreatureId Id)
{
  if (ControlledCreatureId == Id)
  {
    ControlledCreatureId = PlayerCreatureId;
  }
  if (PlayerCreatureId == Id)
  {
    PlayerCreatureId = 0;
  }
  ActivityDirector.OnCreatureRemoved(Id);
  Creatures.erase(Id);
}

void UWorld::ForEachCreature(const std::function<void(UCreature &)> &fn)
{
  for (auto &entry : Creatures)
  {
    if (entry.second)
    {
      fn(*entry.second);
    }
  }
}

void UWorld::ForEachCreature(
    const std::function<void(const UCreature &)> &fn) const
{
  for (const auto &entry : Creatures)
  {
    if (entry.second)
    {
      fn(*entry.second);
    }
  }
}

std::string UWorld::ResolveAnimationTypeId(const UCreature &creature) const
{
  if (creature.IsPlayerCharacter())
  {
    if (auto user = GetCurrentUser())
    {
      const std::string &appearance = user->GetSelectedAppearanceTypeId();
      if (!appearance.empty())
      {
        return appearance;
      }
    }
  }
  return creature.GetTypeId();
}

const CreatureDefinition *
UWorld::GetCreatureDefinition(const std::string &typeId) const
{
  if (!CreatureDefinitions)
  {
    return nullptr;
  }
  return CreatureDefinitions->Get(typeId);
}

ResolvedCreatureAppearance
UWorld::GetResolvedAppearance(const UCreature &creature) const
{
  if (!CreatureDefinitions)
  {
    return ResolvedCreatureAppearance{};
  }
  USkinDefinitionStorage empty;
  const USkinDefinitionStorage &skins =
      SkinDefinitions ? *SkinDefinitions : empty;
  std::string skinId = creature.GetSkinId();
  if (skinId.empty() && creature.IsPlayerCharacter())
  {
    if (auto user = GetCurrentUser())
    {
      if (!user->GetSelectedSkinId().empty())
      {
        skinId = user->GetSelectedSkinId();
      }
      else if (!user->GetSelectedAppearanceTypeId().empty())
      {
        skinId = user->GetSelectedAppearanceTypeId();
      }
    }
  }
  return ResolveCreatureAppearance(*CreatureDefinitions, skins,
                                   creature.GetTypeId(), skinId);
}

namespace
{

bool RayIntersectsAabb(const glm::vec3 &origin, const glm::vec3 &dir,
                       const glm::vec3 &boxMin, const glm::vec3 &boxMax,
                       float maxDistance, float &outT)
{
  float tmin = 0.0f;
  float tmax = maxDistance;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (std::abs(dir[axis]) < 1e-6f)
    {
      if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
      {
        return false;
      }
      continue;
    }
    const float inv = 1.0f / dir[axis];
    float t1 = (boxMin[axis] - origin[axis]) * inv;
    float t2 = (boxMax[axis] - origin[axis]) * inv;
    if (t1 > t2)
    {
      std::swap(t1, t2);
    }
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);
    if (tmin > tmax)
    {
      return false;
    }
  }
  outT = tmin;
  return tmin >= 0.0f && tmin <= maxDistance;
}

glm::vec3 ComputeSpawnBodyOriginAhead(UWorld &world)
{
  const float eyeHeight = ResolveViewerEyeHeight(world);
  const glm::vec3 eyeOffset(0.0f, eyeHeight, 0.0f);
  glm::vec3 bodyOrigin = world.GetSpawnPoint();
  bodyOrigin.y -= eyeOffset.y;
  if (auto camera = world.GetCurrentUserCamera())
  {
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      bodyOrigin =
          camera->GetPosition() - eyeOffset + glm::normalize(forward) * 3.0f;
    }
    else
    {
      bodyOrigin =
          camera->GetPosition() - eyeOffset + glm::vec3(3.0f, 0.0f, 0.0f);
    }
  }
  else if (UCreature *player = world.GetPlayerCreature())
  {
    bodyOrigin = player->GetBodyOrigin() + glm::vec3(3.0f, 0.0f, 0.0f);
  }
  return bodyOrigin;
}

glm::vec3 ComputeSpawnProbeOrigin(UWorld &world, const CreatureDefinition &def)
{
  const glm::vec3 viewProbe = ComputeSpawnBodyOriginAhead(world);
  return SnapSpawnProbeToHabitat(world, def, viewProbe);
}

} // namespace

bool UWorld::SpawnCreatureByView(const std::string &speciesId)
{
  const CreatureDefinition *def =
      CreatureDefinitions ? CreatureDefinitions->Get(speciesId) : nullptr;
  if (!def)
  {
    return false;
  }
  if (!def->catalog.spawnable && def->role != CreatureRole::ControlledDefault)
  {
    return false;
  }
  const glm::vec3 bodyOrigin = ComputeSpawnProbeOrigin(*this, *def);
  if (!CanSpawnCreatureAt(*this, *def, bodyOrigin))
  {
    return false;
  }
  const glm::vec3 adjusted = AdjustSpawnBodyOrigin(*this, *def, bodyOrigin);
  return SpawnCreature(speciesId, adjusted) != 0;
}

bool UWorld::CanSpawnCreatureByView(const std::string &speciesId)
{
  const CreatureDefinition *def =
      CreatureDefinitions ? CreatureDefinitions->Get(speciesId) : nullptr;
  if (!def)
  {
    return false;
  }
  if (!def->catalog.spawnable && def->role != CreatureRole::ControlledDefault)
  {
    return false;
  }
  const glm::vec3 bodyOrigin = ComputeSpawnProbeOrigin(*this, *def);
  return CanSpawnCreatureAt(*this, *def, bodyOrigin);
}

std::string UWorld::GetCreatureSpawnBlockedHint(const std::string &speciesId)
{
  const CreatureDefinition *def =
      CreatureDefinitions ? CreatureDefinitions->Get(speciesId) : nullptr;
  if (!def)
  {
    return u8"Неизвестный вид";
  }
  if (!def->catalog.spawnable && def->role != CreatureRole::ControlledDefault)
  {
    return u8"Нельзя спавнить";
  }
  const glm::vec3 bodyOrigin = ComputeSpawnProbeOrigin(*this, *def);
  return ::cutum::GetCreatureSpawnBlockedHint(*this, *def, bodyOrigin);
}

std::optional<CreatureId> UWorld::PickCreatureByView(const glm::vec3 &eye,
                                                     const glm::vec3 &front,
                                                     float maxDistance) const
{
  if (glm::length(front) < 1e-4f)
  {
    return std::nullopt;
  }
  const glm::vec3 dir = glm::normalize(front);
  float bestT = std::numeric_limits<float>::max();
  CreatureId bestId = 0;
  ForEachCreature(
      [&](const UCreature &creature)
      {
        const glm::vec3 size = creature.GetBounds().profile.restSizeBlocks;
        const glm::vec3 center =
            BoundsCollisionCenter(creature.GetBodyOrigin(), size);
        const glm::vec3 half = size * 0.5f;
        const glm::vec3 boxMin = center - half;
        const glm::vec3 boxMax = center + half;
        float t = 0.0f;
        if (!RayIntersectsAabb(eye, dir, boxMin, boxMax, maxDistance, t))
        {
          return;
        }
        if (t < bestT)
        {
          bestT = t;
          bestId = creature.GetId();
        }
      });
  if (bestId == 0)
  {
    return std::nullopt;
  }
  return bestId;
}

bool UWorld::TryApplySkin(CreatureId target, const std::string &skinId,
                          std::string *outError)
{
  auto setError = [&](const std::string &msg)
  {
    if (outError)
    {
      *outError = msg;
    }
  };
  UCreature *creature = GetCreature(target);
  if (!creature)
  {
    setError("UCreature not found");
    return false;
  }
  if (!SkinDefinitions)
  {
    setError("Skin catalog not loaded");
    return false;
  }
  const SkinDefinition *skin = SkinDefinitions->Get(skinId);
  if (!skin)
  {
    setError("Unknown skin");
    return false;
  }
  if (!SkinDefinitions->IsCompatible(skinId, creature->GetTypeId()))
  {
    setError("Skin is not compatible with this species");
    return false;
  }
  creature->SetSkinId(skinId);
  if (const CreatureDefinition *def =
          GetCreatureDefinition(creature->GetTypeId()))
  {
    creature->SetVisual(CreateCreatureVisual(*def));
  }
  return true;
}

void UWorld::LinkUsersToPlayerCreatures()
{
  for (auto &entry : Users)
  {
    if (entry.second && entry.second->GetPlayerCreatureId() == 0 &&
        PlayerCreatureId != 0)
    {
      entry.second->SetPlayerCreatureId(PlayerCreatureId);
    }
  }
}

namespace
{

void RemapLegacySpeciesType(std::string &type)
{
  if (type == "test_mob" || type == "scout")
  {
    type = "sheep";
  }
  else if (type == "brute")
  {
    type = "sand_monster";
  }
  else if (type == "drifter")
  {
    type = "wolf";
  }
}

void RemapLegacySkinId(std::string &skinId)
{
  if (skinId == "scout_golden")
  {
    skinId = "sheep_wool_golden";
  }
  else if (skinId == "drifter_ice")
  {
    skinId = "wolf_snow";
  }
  else if (skinId == "brute_rust")
  {
    skinId.clear();
  }
}

} // namespace

void UWorld::LoadCreatures(const std::string &file_name)
{
  if (!std::filesystem::exists(file_name))
  {
    return;
  }
  try
  {
    std::ifstream file(file_name);
    json data;
    file >> data;
    if (!data.contains("creatures") || !data["creatures"].is_array())
    {
      return;
    }
    ActivityDirector.Clear();
    for (const auto &c : data["creatures"])
    {
      const CreatureId id = c.value("id", 0);
      if (id == 0)
      {
        continue;
      }
      std::string type = c.value("type", "sheep");
      RemapLegacySpeciesType(type);
      if (type == "player")
      {
        continue;
      }
      std::string skin = c.value("skin_id", "");
      RemapLegacySkinId(skin);
      glm::vec3 bodyOrigin(0.0f);
      if (c.contains("body_origin") && c["body_origin"].is_array() &&
          c["body_origin"].size() == 3)
      {
        bodyOrigin = glm::vec3(c["body_origin"][0].get<float>(),
                               c["body_origin"][1].get<float>(),
                               c["body_origin"][2].get<float>());
      }
      const CreatureDefinition *def = CreatureDefinitions->Get(type);
      if (!def)
      {
        std::cerr << "LoadCreatures: unknown type '" << type
                  << "', falling back to sheep" << std::endl;
        type = "sheep";
      }
      def = CreatureDefinitions->Get(type);
      if (!def)
      {
        continue;
      }
      const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);
      auto creature =
          std::make_unique<UCreature>(id, type, bodyOrigin, eyeOffset);
      creature->GetBoundsMutable().profile = def->bounds;
      creature->GetBoundsMutable().currentSizeBlocks =
          def->bounds.restSizeBlocks;
      creature->SetCapabilities(def->locomotion);
      creature->SetLocomotionArchetype(def->locomotionArchetype);
      creature->SetModelYawOffsetDeg(def->visual.modelYawOffsetDeg);
      creature->SetWalkCycleHz(def->visual.Animation.walkCycleHz);
      creature->GetLocomotion().SetCollisionProfile(
          creature->GetBounds().currentSizeBlocks, def->eyeHeight);
      if (!skin.empty())
      {
        creature->SetSkinId(skin);
      }
      creature->SetVisual(CreateCreatureVisual(*def));
      creature->SetOrientation(c.value("yaw", 0.0f), c.value("pitch", 0.0f));
      if (def->habitat != CreatureHabitat::Terrestrial ||
          c.value("movement_mode", "walking") == "flying")
      {
        creature->GetLocomotion().SetMode(CreatureMovementMode::Flying);
      }
      creature->GetInventory().DeserializeFromJson(c);
      if (def->habitat == CreatureHabitat::Terrestrial)
      {
        SnapCreatureFeetToGround(*creature);
      }
      Creatures[id] = std::move(creature);
      ActivityDirector.OnCreatureAdded(id, def->behavior.Id);
      NextCreatureId = std::max(NextCreatureId, id + 1);
    }
    if (PlayerCreatureId != 0 && ControlledCreatureId == 0)
    {
      ControlledCreatureId = PlayerCreatureId;
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "LoadCreatures: " << e.what() << std::endl;
  }
}

void UWorld::SaveCreatures(const std::string &file_name)
{
  json root;
  root["format_version"] = 1;
  json arr = json::array();
  for (const auto &entry : Creatures)
  {
    if (!entry.second || entry.second->IsPlayerCharacter())
    {
      continue;
    }
    const UCreature &c = *entry.second;
    json item;
    item["id"] = c.GetId();
    item["type"] = c.GetTypeId();
    if (!c.GetSkinId().empty())
    {
      item["skin_id"] = c.GetSkinId();
    }
    const glm::vec3 body = c.GetBodyOrigin();
    item["body_origin"] = json::array({body.x, body.y, body.z});
    item["yaw"] = c.GetYaw();
    item["pitch"] = c.GetPitch();
    item["movement_mode"] = c.GetMovementMode() == CreatureMovementMode::Flying
                                ? "flying"
                                : "walking";
    c.GetInventory().SerializeToJson(item);
    arr.push_back(item);
  }
  root["creatures"] = arr;
  std::ofstream file(file_name);
  file << root.dump(2);
}

} // namespace cutum
