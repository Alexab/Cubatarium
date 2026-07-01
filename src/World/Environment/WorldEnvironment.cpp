#include "World/Environment/WorldEnvironment.h"
#include <unordered_set>

#include "Activity/CreatureActivityTypes.h"
#include "Activity/CreatureActivityRegistry.h"
#include "Activity/IUWorldPerception.h"
#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Pose/CreaturePosePresenterRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Primitives/Cube.h"
#include "World/Core/World.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <vector>

namespace cutum
{

using json = nlohmann::json;

UWorldEnvironment::UWorldEnvironment(UWorld &owner) : Owner(owner) {}

void UWorldEnvironment::Initialize()
{
  RegisterDefaultCreaturePosePresenters(PosePresenterRegistry);
}

void UWorldEnvironment::SetCreatureDefinitionStorage(
    std::shared_ptr<UCreatureDefinitionStorage> storage)
{
  CreatureDefinitions = std::move(storage);
  RegisterDefaultActivityAgents();
}

void UWorldEnvironment::SetSkinDefinitionStorage(
    std::shared_ptr<USkinDefinitionStorage> storage)
{
  SkinDefinitions = std::move(storage);
}

void UWorldEnvironment::RegisterDefaultActivityAgents()
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
UWorldEnvironment::QueryControlledCreatureInfo() const
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

void UWorldEnvironment::SyncCreatureSpatialIndex()
{
  std::unordered_set<CreatureId> alive;
  ForEachCreature(
      [&](const UCreature &creature)
      {
        if (creature.IsPlayerCharacter())
        {
          return;
        }
        const CreatureId id = creature.GetId();
        alive.insert(id);
        const CollisionVolume vol = creature.GetCollisionVolume();
        CreatureSpatialIndex.Upsert(id, creature.GetBodyOrigin(), vol.halfExtents);
      });
  CreatureSpatialIndex.PruneExcept(alive);
  CreatureSpatialIndexReady = true;
}

std::vector<CreatureId>
UWorldEnvironment::CreaturesInRadius(const glm::vec3 &center,
                                     float radius) const
{
  if (CreatureSpatialIndexReady)
  {
    return CreatureSpatialIndex.QueryRadius(center, radius, 0);
  }
  std::vector<CreatureId> out;
  const float radius_sq = radius * radius;
  ForEachCreature(
      [&](const UCreature &creature)
      {
        if (creature.IsPlayerCharacter())
        {
          return;
        }
        const glm::vec3 delta = creature.GetBodyOrigin() - center;
        const float dist_sq = delta.x * delta.x + delta.z * delta.z;
        if (dist_sq <= radius_sq)
        {
          out.push_back(creature.GetId());
        }
      });
  return out;
}

std::vector<CreatureNeighborView>
UWorldEnvironment::QueryCreatureNeighborsInRadius(const glm::vec3 &center,
                                                  float radius,
                                                  CreatureId skip_id) const
{
  if (CreatureSpatialIndexReady)
  {
    return CreatureSpatialIndex.QueryNeighbors(center, radius, skip_id);
  }
  std::vector<CreatureNeighborView> out;
  const float radius_sq = radius * radius;
  ForEachCreature(
      [&](const UCreature &creature)
      {
        if (creature.GetId() == skip_id || creature.IsPlayerCharacter())
        {
          return;
        }
        const glm::vec3 origin = creature.GetBodyOrigin();
        const glm::vec3 delta = origin - center;
        const float dist_sq = delta.x * delta.x + delta.z * delta.z;
        if (dist_sq <= radius_sq)
        {
          CreatureNeighborView neighbor;
          neighbor.Id = creature.GetId();
          neighbor.bodyOrigin = origin;
          out.push_back(neighbor);
        }
      });
  return out;
}

void UWorldEnvironment::ClearCreatures()
{
  ActivityDirector.Clear();
  Creatures.clear();
  CreatureSpatialIndex.Clear();
  CreatureSpatialIndexReady = false;
  NextCreatureId = 1;
  PlayerCreatureId = 0;
  ControlledCreatureId = 0;
}

void UWorldEnvironment::ResetBeforeEntityLoad() { ClearCreatures(); }

UCreature *UWorldEnvironment::GetCreature(CreatureId id)
{
  const auto it = Creatures.find(id);
  return it != Creatures.end() ? it->second.get() : nullptr;
}

const UCreature *UWorldEnvironment::GetCreature(CreatureId id) const
{
  const auto it = Creatures.find(id);
  return it != Creatures.end() ? it->second.get() : nullptr;
}

UCreature *UWorldEnvironment::GetControlledCreature()
{
  return GetCreature(ControlledCreatureId);
}

const UCreature *UWorldEnvironment::GetControlledCreature() const
{
  return GetCreature(ControlledCreatureId);
}

UCreature *UWorldEnvironment::GetPlayerCreature()
{
  return GetCreature(PlayerCreatureId);
}

const UCreature *UWorldEnvironment::GetPlayerCreature() const
{
  return GetCreature(PlayerCreatureId);
}

bool UWorldEnvironment::SetControlledCreature(CreatureId id)
{
  if (id == 0 || !GetCreature(id))
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
  ControlledCreatureId = id;
  if (UCreature *creature = GetCreature(id))
  {
    creature->SetPossessed(true);
    if (const CreatureDefinition *def =
            GetCreatureDefinition(creature->GetTypeId()))
    {
      if (auto camera = Owner.GetCurrentUserCamera())
      {
        Owner.ApplyLocomotionDefinitionToCamera(*camera, *def);
      }
    }
  }
  return true;
}

void UWorldEnvironment::SnapCreatureFeetToGround(UCreature &creature) const
{
  const glm::vec3 origin = creature.GetBodyOrigin();
  const int gx = WorldCoordToBlockIndex(origin.x);
  const int gz = WorldCoordToBlockIndex(origin.z);
  if (const std::optional<float> feetY =
          Owner.QueryGroundFeetYUnder(gx, gz, origin.y))
  {
    creature.SetBodyOrigin(glm::vec3(origin.x, *feetY, origin.z));
  }
  else if (const std::optional<float> feetY =
               Owner.QueryGroundFeetYColumn(gx, gz))
  {
    creature.SetBodyOrigin(glm::vec3(origin.x, *feetY, origin.z));
  }
}

CreatureId UWorldEnvironment::SpawnCreature(const std::string &speciesId,
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

  const glm::vec3 spawnOrigin = AdjustSpawnBodyOrigin(Owner, *def, bodyOrigin);
  const glm::vec3 spawnSize = def->bounds.restSizeBlocks;
  glm::vec3 resolvedSpawn =
      cutum::TryDepenetrateSpawnOrigin(Owner, def->habitat, spawnOrigin,
                                       spawnSize, 0);
  if (def->role != CreatureRole::ControlledDefault)
  {
    if (!cutum::HabitatAllowsAtForSpawn(Owner, def->habitat, resolvedSpawn,
                                        spawnSize))
    {
      std::cerr << "SpawnCreature: invalid habitat for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
    const CollisionVolume vol =
        CollisionVolumeFromBody(resolvedSpawn, spawnSize);
    CollisionVolume spawnVol = vol;
    constexpr float kSpawnCollisionInset = 0.05f;
    spawnVol.halfExtents -= glm::vec3(kSpawnCollisionInset);
    spawnVol.halfExtents = glm::max(spawnVol.halfExtents, glm::vec3(0.05f));
    if (Owner.CheckCreatureCollisionVolume(spawnVol, 0) ||
        Owner.CheckBlockCollisionVolume(spawnVol))
    {
      std::cerr << "SpawnCreature: no space for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
  }

  const CreatureId id = NextCreatureId++;
  const glm::vec3 eyeOffset(0.0f, def->eyeHeight, 0.0f);

  std::unique_ptr<UCreature> creature;
  if (def->role == CreatureRole::ControlledDefault)
  {
    creature = std::make_unique<UPlayer>(id, speciesId, resolvedSpawn);
  }
  else
  {
    creature =
        std::make_unique<UCreature>(id, speciesId, resolvedSpawn, eyeOffset);
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
  Creatures[id] = std::move(creature);
  if (def->habitat == CreatureHabitat::Terrestrial)
  {
    SnapCreatureFeetToGround(*Creatures[id]);
  }
  else if (def->habitat == CreatureHabitat::Amphibious)
  {
    const EnvironmentSample env = ProbeEnvironmentAt(
        Owner, Creatures[id]->GetBodyOrigin(), def->bounds.restSizeBlocks);
    if (!env.inWater)
    {
      SnapCreatureFeetToGround(*Creatures[id]);
    }
  }
  if (def->role != CreatureRole::ControlledDefault)
  {
    ActivityDirector.OnCreatureAdded(id, def->behavior.Id);
  }
  if (!Creatures[id]->IsPlayerCharacter())
  {
    const CollisionVolume vol = Creatures[id]->GetCollisionVolume();
    CreatureSpatialIndex.Upsert(id, Creatures[id]->GetBodyOrigin(), vol.halfExtents);
    CreatureSpatialIndexReady = true;
  }
  return id;
}

void UWorldEnvironment::RemoveCreature(CreatureId id)
{
  if (ControlledCreatureId == id)
  {
    ControlledCreatureId = PlayerCreatureId;
  }
  if (PlayerCreatureId == id)
  {
    PlayerCreatureId = 0;
  }
  ActivityDirector.OnCreatureRemoved(id);
  CreatureSpatialIndex.Remove(id);
  Creatures.erase(id);
}

void UWorldEnvironment::ForEachCreature(
    const std::function<void(UCreature &)> &fn)
{
  for (auto &entry : Creatures)
  {
    if (entry.second)
    {
      fn(*entry.second);
    }
  }
}

void UWorldEnvironment::ForEachCreature(
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

std::string
UWorldEnvironment::ResolveAnimationTypeId(const UCreature &creature) const
{
  if (creature.IsPlayerCharacter())
  {
    if (auto user = Owner.GetCurrentUser())
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
UWorldEnvironment::GetCreatureDefinition(const std::string &typeId) const
{
  if (!CreatureDefinitions)
  {
    return nullptr;
  }
  return CreatureDefinitions->Get(typeId);
}

ResolvedCreatureAppearance
UWorldEnvironment::GetResolvedAppearance(const UCreature &creature) const
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
    if (auto user = Owner.GetCurrentUser())
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

std::vector<glm::vec3> BuildSpawnProbeCandidates(const glm::vec3 &baseOrigin)
{
  return {
      baseOrigin,
      baseOrigin + glm::vec3(1.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(-1.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, 1.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, -1.0f),
      baseOrigin + glm::vec3(2.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(-2.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, 2.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, -2.0f),
      baseOrigin + glm::vec3(3.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(-3.0f, 0.0f, 0.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, 3.0f),
      baseOrigin + glm::vec3(0.0f, 0.0f, -3.0f),
      baseOrigin + glm::vec3(1.0f, 0.0f, 1.0f),
      baseOrigin + glm::vec3(-1.0f, 0.0f, 1.0f),
      baseOrigin + glm::vec3(1.0f, 0.0f, -1.0f),
      baseOrigin + glm::vec3(-1.0f, 0.0f, -1.0f),
      baseOrigin + glm::vec3(0.0f, 1.0f, 0.0f),
      baseOrigin + glm::vec3(0.0f, 2.0f, 0.0f),
      baseOrigin + glm::vec3(1.0f, 1.0f, 0.0f),
      baseOrigin + glm::vec3(-1.0f, 1.0f, 0.0f),
      baseOrigin + glm::vec3(0.0f, 1.0f, 1.0f),
      baseOrigin + glm::vec3(0.0f, 1.0f, -1.0f),
  };
}

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

bool UWorldEnvironment::SpawnCreatureByView(const std::string &speciesId)
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
  const glm::vec3 baseOrigin = ComputeSpawnProbeOrigin(Owner, *def);
  for (const glm::vec3 &probe : BuildSpawnProbeCandidates(baseOrigin))
  {
    if (SpawnCreature(speciesId, probe) != 0)
    {
      return true;
    }
  }
  return false;
}

bool UWorldEnvironment::CanSpawnCreatureByView(const std::string &speciesId)
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
  const glm::vec3 baseOrigin = ComputeSpawnProbeOrigin(Owner, *def);
  for (const glm::vec3 &probe : BuildSpawnProbeCandidates(baseOrigin))
  {
    if (CanSpawnCreatureAt(Owner, *def, probe))
    {
      return true;
    }
  }
  return false;
}

std::string
UWorldEnvironment::GetCreatureSpawnBlockedHint(const std::string &speciesId)
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
  const glm::vec3 baseOrigin = ComputeSpawnProbeOrigin(Owner, *def);
  for (const glm::vec3 &probe : BuildSpawnProbeCandidates(baseOrigin))
  {
    if (CanSpawnCreatureAt(Owner, *def, probe))
    {
      return {};
    }
  }
  return ::cutum::GetCreatureSpawnBlockedHint(Owner, *def, baseOrigin);
}

std::optional<CreatureId>
UWorldEnvironment::PickCreatureByView(const glm::vec3 &eye,
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

bool UWorldEnvironment::TryApplySkin(CreatureId target,
                                     const std::string &skinId,
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

void UWorldEnvironment::LinkUsersToPlayerCreatures(
    std::map<std::string, std::shared_ptr<UUser>> &users)
{
  for (auto &entry : users)
  {
    if (entry.second && entry.second->GetPlayerCreatureId() == 0 &&
        PlayerCreatureId != 0)
    {
      entry.second->SetPlayerCreatureId(PlayerCreatureId);
    }
  }
}

void UWorldEnvironment::LoadCreatures(const std::string &file_name)
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
      glm::vec3 loaded_origin = creature->GetBodyOrigin();
      loaded_origin = cutum::TryDepenetrateSpawnOrigin(
          Owner, def->habitat, loaded_origin, def->bounds.restSizeBlocks, id);
      creature->SetBodyOrigin(loaded_origin);
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
    SyncCreatureSpatialIndex();
  }
  catch (const std::exception &e)
  {
    std::cerr << "LoadCreatures: " << e.what() << std::endl;
  }
}

void UWorldEnvironment::SaveCreatures(const std::string &file_name)
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
    const UCreature &creature = *entry.second;
    json item;
    item["id"] = creature.GetId();
    item["type"] = creature.GetTypeId();
    if (!creature.GetSkinId().empty())
    {
      item["skin_id"] = creature.GetSkinId();
    }
    const glm::vec3 body = creature.GetBodyOrigin();
    item["body_origin"] = json::array({body.x, body.y, body.z});
    item["yaw"] = creature.GetYaw();
    item["pitch"] = creature.GetPitch();
    item["movement_mode"] = creature.GetMovementMode() == CreatureMovementMode::Flying
                                ? "flying"
                                : "walking";
    creature.GetInventory().SerializeToJson(item);
    arr.push_back(item);
  }
  root["creatures"] = arr;
  std::ofstream file(file_name);
  file << root.dump(2);
}

void UWorldEnvironment::ReloadAllCreatureVisuals()
{
  ForEachCreature(
      [this](UCreature &creature)
      {
        const CreatureDefinition *def =
            GetCreatureDefinition(creature.GetTypeId());
        if (!def)
        {
          return;
        }
        creature.SetVisual(CreateCreatureVisual(*def));
      });
}

void UWorldEnvironment::TickActivity(IUWorldPerception &perception,
                                     UWorldCreatureActivitySink &sink,
                                     float dt)
{
  SyncCreatureSpatialIndex();
  ActivityDirector.TickAgents(perception, sink, dt);
}

bool UWorldEnvironment::CheckCreatureCollisionVolume(
    const CollisionVolume &vol, CreatureId skipCreatureId) const
{
  if (CreatureSpatialIndexReady)
  {
    return CreatureSpatialIndex.AnyCreatureVolumeOverlaps(vol, skipCreatureId);
  }
  for (const auto &entry : Creatures)
  {
    if (entry.first == skipCreatureId)
    {
      continue;
    }
    const CollisionVolume other = entry.second->GetCollisionVolume();
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, other.center,
                                  other.halfExtents))
    {
      return true;
    }
  }
  return false;
}

} // namespace cutum
