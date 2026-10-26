#include "World/Environment/WorldEnvironment.h"
#include <unordered_set>

#include "Activity/CreatureActivityTypes.h"
#include "Activity/CreatureActivityRegistry.h"
#include "Activity/IUWorldPerception.h"
#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/UCreatureFactory.h"
#include "Creatures/Core/CreaturePersistenceJson.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/User.h"
#include "Creatures/Spawning/CreaturePopulationRules.h"
#include "Creatures/Spawning/CreatureLoadRecovery.h"
#include "Creatures/Spawning/CreatureSpawnSelection.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Pose/CreaturePosePresenterRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Primitives/Cube.h"
#include "World/Core/World.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include "World/Chunks/ChunkManager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <vector>

namespace cutum
{

namespace
{

/// Bumped when creatures.json layout changes; old versions keep loading via
/// the migration notes in LoadCreatures.
constexpr int kCreatureSaveFormatVersion = 2;

/// Hard ceiling on simultaneously existing creatures, on top of the mode and
/// population rules in CreaturePopulationRules.h.
constexpr size_t kAbsoluteCreatureLimit = 128;

} // namespace

using json = nlohmann::json;

namespace
{
bool IsCreatureVolumeLoadedAt(const UWorld &world, const glm::vec3 &origin,
                              const glm::vec3 &sizeBlocks)
{
  const CollisionVolume volume = CollisionVolumeFromBody(origin, sizeBlocks);
  const glm::ivec3 minChunk = UChunkManager::WorldToChunk(
      WorldPosToBlock(volume.center - volume.halfExtents));
  const glm::ivec3 maxChunk = UChunkManager::WorldToChunk(
      WorldPosToBlock(volume.center + volume.halfExtents));
  const UChunkManager &chunks = world.GetBlockWorld().GetChunkManager();
  for (int x = minChunk.x; x <= maxChunk.x; ++x)
  {
    for (int y = minChunk.y; y <= maxChunk.y; ++y)
    {
      for (int z = minChunk.z; z <= maxChunk.z; ++z)
      {
        if (!chunks.HasChunk(glm::ivec3(x, y, z)))
        {
          return false;
        }
      }
    }
  }
  return true;
}

} // namespace

UWorldEnvironment::UWorldEnvironment(UWorld &owner)
    : Owner(owner), SpawnPlanner(NaturalPopulationRng) {}

void UWorldEnvironment::Initialize()
{
  RegisterDefaultCreaturePosePresenters(PosePresenterRegistry);
  NaturalPopulationRng.seed(Owner.GetWorldSeed());
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
  struct RebindActivity
  {
    CreatureId Id{0};
    std::string behaviorId;
    CreatureBehaviorParams behavior;
  };
  std::vector<RebindActivity> rebind;
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
    rebind.push_back({entry.first, def->behavior.Id, def->behavior});
  }
  ActivityDirector.Clear();
  RegisterDefaultCreatureActivityAgents(ActivityDirector);
  for (const auto &pair : rebind)
  {
    ActivityDirector.OnCreatureAdded(pair.Id, pair.behaviorId, &pair.behavior);
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
  info.bodyOrigin = controlled->GetBodyOrigin();
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

void UWorldEnvironment::UpdateCreatureInSpatialIndex(
    const UCreature &creature)
{
  if (creature.IsPlayerCharacter())
  {
    return;
  }
  const CollisionVolume vol = creature.GetCollisionVolume();
  CreatureSpatialIndex.Upsert(creature.GetId(), creature.GetBodyOrigin(),
                              vol.halfExtents);
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
UWorldEnvironment::QueryCreatureNeighborsOverlappingVolume(
    const CollisionVolume &vol, CreatureId skip_id) const
{
  if (CreatureSpatialIndexReady)
  {
    return CreatureSpatialIndex.QueryNeighborsOverlappingVolume(vol,
                                                                skip_id);
  }
  std::vector<CreatureNeighborView> out;
  for (const auto &entry : Creatures)
  {
    if (entry.first == skip_id)
    {
      continue;
    }
    const CollisionVolume other = entry.second->GetCollisionVolume();
    if (UCube::CheckAabbCollision(vol.center, vol.halfExtents, other.center,
                                  other.halfExtents))
    {
      CreatureNeighborView view;
      view.Id = entry.first;
      view.bodyOrigin = entry.second->GetBodyOrigin();
      out.push_back(view);
    }
  }
  return out;
}

bool UWorldEnvironment::AnyCreatureVolumeOverlapsExcluding(
    const CollisionVolume &vol, CreatureId skipA, CreatureId skipB) const
{
  for (const CreatureNeighborView &neighbor :
       QueryCreatureNeighborsOverlappingVolume(vol, skipA))
  {
    if (neighbor.Id == skipB)
    {
      continue;
    }
    return true;
  }
  return false;
}

std::optional<std::pair<CreatureId, float>>
UWorldEnvironment::FindMountUnder(const glm::vec3 &feetPos,
                                  const glm::vec3 &halfExtents,
                                  CreatureId skipId) const
{
  // Riding threshold (runtime issue 5): backs must be big enough to stand
  // on - tiny flyers/swimmers (owl, fish) are excluded.
  constexpr float kMinMountHeightBlocks = 1.0f;
  constexpr float kMinMountWidthBlocks = 0.9f;
  constexpr float kMountTopToleranceDown = 0.45f;
  constexpr float kMountTopToleranceUp = 0.25f;

  CollisionVolume riderVol;
  riderVol.center = feetPos + glm::vec3(0.f, halfExtents.y, 0.f);
  riderVol.halfExtents = halfExtents;
  std::optional<std::pair<CreatureId, float>> best;
  for (const CreatureNeighborView &neighbor :
       QueryCreatureNeighborsOverlappingVolume(riderVol, skipId))
  {
    const UCreature *candidate = GetCreature(neighbor.Id);
    if (!candidate || candidate->IsPlayerCharacter() ||
        candidate->IsPossessed())
    {
      continue;
    }
    const CollisionVolume body = candidate->GetCollisionVolume();
    if (body.halfExtents.y * 2.f < kMinMountHeightBlocks ||
        std::max(body.halfExtents.x, body.halfExtents.z) * 2.f <
            kMinMountWidthBlocks)
    {
      continue;
    }
    const float backY = body.center.y + body.halfExtents.y;
    if (backY < feetPos.y - kMountTopToleranceDown ||
        backY > feetPos.y + kMountTopToleranceUp)
    {
      continue;
    }
    const float overlapX =
        riderVol.halfExtents.x + body.halfExtents.x -
        std::abs(riderVol.center.x - body.center.x);
    const float overlapZ =
        riderVol.halfExtents.z + body.halfExtents.z -
        std::abs(riderVol.center.z - body.center.z);
    if (overlapX < 0.05f || overlapZ < 0.05f)
    {
      continue;
    }
    if (!best || backY > best->second)
    {
      best = std::make_pair(neighbor.Id, backY);
    }
  }
  return best;
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
  // Reseed so a world reloaded in one session does not replay the exact
  // same natural-population random stream.
  NaturalPopulationRng.seed(Owner.GetWorldSeed());
  NaturalPopulationTimer = 0.f;
  LoadedCreaturePositionValidationTimer = 0.f;
  LoadedCreaturePositionValidationPending = true;
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
                                            const std::string &skinId,
                                            CreatureSpawnSource source)
{
  const CreatureDefinition *def =
      CreatureDefinitions ? CreatureDefinitions->Get(speciesId) : nullptr;
  if (!def)
  {
    std::cerr << "SpawnCreature: unknown species '" << speciesId << "'"
              << std::endl;
    return 0;
  }

  const bool controlledDefault =
      def->role == CreatureRole::ControlledDefault;
  if (controlledDefault && PlayerCreatureId != 0)
  {
    return 0;
  }
  if (!controlledDefault && Owner.IsHardcoreDead())
  {
    return 0;
  }
  if (!controlledDefault &&
      (!ModePolicy::AllowsCreatureSpawn(Owner.GetGameMode(), source) ||
       !def->catalog.spawnable))
  {
    return 0;
  }
  if (source == CreatureSpawnSource::NaturalPopulation &&
      (!def->catalog.naturalSpawnable ||
       (HasCreatureTag(def->catalog.tags, "hostile") &&
        !ModePolicy::AllowsHostileSpawning(Owner.GetGameMode(),
                                           Owner.GetDifficulty()))))
  {
    return 0;
  }
  if (source == CreatureSpawnSource::NaturalPopulation)
  {
    const UCreature *player = Owner.GetPlayerCreature();
    if (!player)
    {
      return 0;
    }
    const CreaturePopulationCounts counts =
        CountNaturalPopulation(player->GetBodyOrigin());
    if (!CreaturePopulationRules::CanAddNaturalCreature(
            counts, HasCreatureTag(def->catalog.tags, "hostile"),
            Owner.GetDifficulty(), Owner.GetGameMode()))
    {
      return 0;
    }
  }
  if (!controlledDefault && Creatures.size() >= kAbsoluteCreatureLimit)
  {
    return 0;
  }

  const glm::vec3 spawnOrigin = AdjustSpawnBodyOrigin(Owner, *def, bodyOrigin);
  const glm::vec3 spawnSize = def->bounds.restSizeBlocks;
  glm::vec3 resolvedSpawn =
      cutum::TryDepenetrateSpawnOrigin(Owner, def->habitat, spawnOrigin,
                                       spawnSize, 0);
  if (def->role != CreatureRole::ControlledDefault)
  {
    if (source == CreatureSpawnSource::NaturalPopulation &&
        !IsCreatureVolumeLoadedAt(Owner, resolvedSpawn, spawnSize))
    {
      return 0;
    }
    if (!cutum::HabitatAllowsAtForSpawn(Owner, def->habitat, resolvedSpawn,
                                        spawnSize))
    {
      std::cerr << "SpawnCreature: invalid habitat for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
    const CollisionVolume spawnVol = InsetCollisionVolumeForSpawn(
        CollisionVolumeFromBody(resolvedSpawn, spawnSize));
    if (Owner.CheckCreatureCollisionVolume(spawnVol, 0) ||
        Owner.CheckBlockCollisionVolume(spawnVol))
    {
      std::cerr << "SpawnCreature: no space for '" << speciesId << "'"
                << std::endl;
      return 0;
    }
  }

  const CreatureId id = NextCreatureId++;

  CreatureBuildParams buildParams;
  buildParams.Id = id;
  buildParams.bodyOrigin = resolvedSpawn;
  buildParams.Source = source;
  buildParams.SkinId = skinId;
  buildParams.AllowFlight =
      ModePolicy::AllowsFlight(Owner.GetGameMode(), def->habitat);
  Creatures[id] = UCreatureFactory::Build(*def, buildParams);
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
    ActivityDirector.OnCreatureAdded(id, def->behavior.Id, &def->behavior);
  }
  if (!Creatures[id]->IsPlayerCharacter())
  {
    const CollisionVolume vol = Creatures[id]->GetCollisionVolume();
    CreatureSpatialIndex.Upsert(id, Creatures[id]->GetBodyOrigin(), vol.halfExtents);
    CreatureSpatialIndexReady = true;
  }
  return id;
}

CreaturePopulationCounts UWorldEnvironment::CountNaturalPopulation(
    const glm::vec3 &playerOrigin)
{
  // Single scan shared by cap checks and the population tick (audit S4 —
  // the loop was duplicated in SpawnCreature and TickNaturalPopulation).
  CreaturePopulationCounts counts;
  ForEachCreature(
      [&](UCreature &creature)
      {
        if (creature.IsPlayerCharacter())
        {
          return;
        }
        const CreatureDefinition *definition =
            GetCreatureDefinition(creature.GetTypeId());
        const bool hostile =
            definition && HasCreatureTag(definition->catalog.tags, "hostile");
        const bool local =
            glm::distance(creature.GetBodyOrigin(), playerOrigin) <=
            CreaturePopulationRules::LocalRadiusBlocks;
        CreaturePopulationRules::Accumulate(counts, creature.GetSpawnSource(),
                                            hostile, local);
      });
  return counts;
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
  const USkinDefinitionStorage *skins = SkinDefinitions.get();
  const uint64_t defsRevision =
      CreatureDefinitions->Revision.load(std::memory_order_relaxed);
  const uint64_t skinsRevision =
      skins ? skins->Revision.load(std::memory_order_relaxed) : 0;
  if (defsRevision != AppearanceCacheDefsRevision ||
      skinsRevision != AppearanceCacheSkinsRevision ||
      CreatureDefinitions.get() != AppearanceCacheDefsPtr ||
      skins != AppearanceCacheSkinsPtr)
  {
    CreatureAppearanceCache.clear();
    AppearanceCacheDefsRevision = defsRevision;
    AppearanceCacheSkinsRevision = skinsRevision;
    AppearanceCacheDefsPtr = CreatureDefinitions.get();
    AppearanceCacheSkinsPtr = skins;
  }
  USkinDefinitionStorage empty;
  const USkinDefinitionStorage &skinStorage = skins ? *skins : empty;
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
  // Resolving rebuilds part lists with storage lookups; cache per
  // (species, effective skin) — this used to run per creature per frame
  // (audit V6).
  const std::string cacheKey = creature.GetTypeId() + '\x1f' + skinId;
  if (const auto it = CreatureAppearanceCache.find(cacheKey);
      it != CreatureAppearanceCache.end())
  {
    return it->second;
  }
  ResolvedCreatureAppearance resolved = ResolveCreatureAppearance(
      *CreatureDefinitions, skinStorage, creature.GetTypeId(), skinId);
  CreatureAppearanceCache[cacheKey] = resolved;
  return resolved;
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

} // namespace

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

namespace
{

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
  if (!ModePolicy::AllowsCreatureSpawn(Owner.GetGameMode(),
                                       CreatureSpawnSource::CreativeManual))
  {
    return false;
  }
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
    if (SpawnCreature(speciesId, probe, "",
                      CreatureSpawnSource::CreativeManual) != 0)
    {
      return true;
    }
  }
  return false;
}

bool UWorldEnvironment::CanSpawnCreatureByView(const std::string &speciesId)
{
  if (!ModePolicy::AllowsCreatureSpawn(Owner.GetGameMode(),
                                       CreatureSpawnSource::CreativeManual))
  {
    return false;
  }
  if (Owner.IsHardcoreDead() || Creatures.size() >= kAbsoluteCreatureLimit)
  {
    return false;
  }
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
  if (!ModePolicy::AllowsCreatureSpawn(Owner.GetGameMode(),
                                       CreatureSpawnSource::CreativeManual))
  {
    return "Creature palette spawning is only available in Creative";
  }
  if (Owner.IsHardcoreDead())
  {
    return "Creature spawning is disabled in a completed Hardcore world";
  }
  if (Creatures.size() >= kAbsoluteCreatureLimit)
  {
    return "Creature population limit reached";
  }
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
  if (!CreatureDefinitions)
  {
    std::cerr << "LoadCreatures: definition storage is not attached; skipping "
              << file_name << std::endl;
    return;
  }
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
    const int formatVersion = data.value("format_version", 0);
    if (formatVersion == 0)
    {
      std::cerr << "LoadCreatures: no format_version in " << file_name
                << "; loading as pre-versioning save" << std::endl;
    }
    else if (formatVersion > kCreatureSaveFormatVersion)
    {
      std::cerr << "LoadCreatures: format_version " << formatVersion
                << " is newer than supported " << kCreatureSaveFormatVersion
                << "; attempting best-effort load" << std::endl;
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
      const glm::vec3 bodyOrigin =
          ReadCreatureBodyOrigin(c, glm::vec3(0.0f));
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
      CreatureBuildParams buildParams;
      buildParams.Id = id;
      buildParams.bodyOrigin = bodyOrigin;
      buildParams.Source = ReadCreatureSpawnSource(c);
      buildParams.SkinId = skin;
      // Restored creatures clamp flight exactly like fresh spawns (audit S3).
      buildParams.AllowFlight =
          ModePolicy::AllowsFlight(Owner.GetGameMode(), def->habitat);
      buildParams.SavedState = &c;
      buildParams.AllowPlayerRole = false;
      auto creature = UCreatureFactory::Build(*def, buildParams);
      // Cooperative loading restores entities before it reads terrain chunks.
      // Only resolve collision/habitat now if this entity's chunk is present;
      // otherwise retain its saved position and retry as streaming loads it.
      glm::vec3 loaded_origin = creature->GetBodyOrigin();
      bool safePositionResolved = false;
      if (IsCreatureVolumeLoadedAt(Owner, loaded_origin,
                                   def->bounds.restSizeBlocks))
      {
        const auto safeOrigin = FindSafeCreatureOrigin(
            loaded_origin,
            [&](const glm::vec3 &candidate)
            {
              return AdjustSpawnBodyOrigin(Owner, *def, candidate);
            },
            [&](const glm::vec3 &candidate)
            {
              return IsCreatureVolumeLoadedAt(
                  Owner, candidate, def->bounds.restSizeBlocks);
            },
            [&](const glm::vec3 &candidate)
            {
              if (!HabitatAllowsAtForSpawn(Owner, def->habitat, candidate,
                                           def->bounds.restSizeBlocks))
              {
                return false;
              }
              const CollisionVolume volume = InsetCollisionVolumeForSpawn(
                  CollisionVolumeFromBody(candidate, def->bounds.restSizeBlocks));
              return !Owner.CheckBlockCollisionVolume(volume) &&
                     !Owner.CheckCreatureCollisionVolume(volume, id);
            });
        if (safeOrigin)
        {
          loaded_origin = *safeOrigin;
          safePositionResolved = true;
        }
      }
      creature->SetBodyOrigin(loaded_origin);
      if (safePositionResolved &&
          def->habitat == CreatureHabitat::Terrestrial)
      {
        SnapCreatureFeetToGround(*creature);
      }
      Creatures[id] = std::move(creature);
      ActivityDirector.OnCreatureAdded(id, def->behavior.Id, &def->behavior);
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
  root["format_version"] = kCreatureSaveFormatVersion;
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
    WriteCreatureSpawnSource(item, creature.GetSpawnSource());
    if (!creature.GetSkinId().empty())
    {
      item["skin_id"] = creature.GetSkinId();
    }
    WriteCreatureBodyOrigin(item, creature.GetBodyOrigin());
    item["yaw"] = creature.GetYaw();
    item["pitch"] = creature.GetPitch();
    item["movement_mode"] = creature.GetMovementMode() == CreatureMovementMode::Flying
                                ? "flying"
                                : "walking";
    creature.GetInventory().SerializeToJson(item);
    CreatureStatsJson::Write(item, creature.GetVitals(),
                             creature.GetAttributes());
    arr.push_back(item);
  }
  root["creatures"] = arr;

  // Atomic write: dump to a sibling temp file, then rename over the target so
  // a crash mid-save cannot truncate an existing creatures.json.
  const std::filesystem::path target(file_name);
  const std::filesystem::path tmp = target.parent_path() /
                                    (target.filename().string() + ".tmp");
  {
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
      std::cerr << "SaveCreatures: cannot open " << tmp.string()
                << " for writing" << std::endl;
      return;
    }
    file << root.dump(2);
    file.flush();
    if (!file.good())
    {
      std::cerr << "SaveCreatures: write to " << tmp.string() << " failed"
                << std::endl;
      file.close();
      std::error_code ignored;
      std::filesystem::remove(tmp, ignored);
      return;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  if (ec)
  {
    // Windows rename refuses to replace some destination states; fall back to
    // remove+rename, which is not atomic but still leaves one complete file.
    std::filesystem::remove(target, ec);
    std::filesystem::rename(tmp, target, ec);
  }
  if (ec)
  {
    std::cerr << "SaveCreatures: could not move " << tmp.string() << " to "
              << target.string() << ": " << ec.message() << std::endl;
  }
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
                                     float dt, bool stress_tick)
{
  // The index is maintained incrementally: spawn/remove and per-move
  // upserts keep it exact, so the per-activity-tick full rebuild is gone
  // (audit S7). ValidateLoadedCreaturePositions still rebuilds after
  // repairing positions.
  if (!stress_tick)
  {
    LoadedCreaturePositionValidationTimer += std::max(0.f, dt);
    if (LoadedCreaturePositionValidationPending ||
        LoadedCreaturePositionValidationTimer >= 1.f)
    {
      LoadedCreaturePositionValidationTimer =
          std::fmod(LoadedCreaturePositionValidationTimer, 1.f);
      LoadedCreaturePositionValidationPending = false;
      ValidateLoadedCreaturePositions();
    }
  }
  ActivityDirector.TickAgents(perception, sink, dt, stress_tick);
}

void UWorldEnvironment::ValidateLoadedCreaturePositions()
{
  if (!CreatureDefinitions)
  {
    return;
  }
  // Position changes during this pass must be visible to collision checks for
  // later creatures; use the live map while repairing, then rebuild the index.
  CreatureSpatialIndex.Clear();
  CreatureSpatialIndexReady = false;
  ForEachCreature(
      [this](UCreature &creature)
      {
        if (creature.IsPlayerCharacter())
        {
          return;
        }
        const CreatureDefinition *definition =
            GetCreatureDefinition(creature.GetTypeId());
        if (!definition ||
            !IsCreatureVolumeLoadedAt(Owner, creature.GetBodyOrigin(),
                                      definition->bounds.restSizeBlocks))
        {
          return;
        }
        const auto safeOrigin = FindSafeCreatureOrigin(
            creature.GetBodyOrigin(),
            [&](const glm::vec3 &candidate)
            {
              return AdjustSpawnBodyOrigin(Owner, *definition, candidate);
            },
            [&](const glm::vec3 &candidate)
            {
              return IsCreatureVolumeLoadedAt(
                  Owner, candidate, definition->bounds.restSizeBlocks);
            },
            [&](const glm::vec3 &candidate)
            {
              if (!HabitatAllowsAtForSpawn(
                      Owner, definition->habitat, candidate,
                      definition->bounds.restSizeBlocks))
              {
                return false;
              }
              const CollisionVolume volume = InsetCollisionVolumeForSpawn(
                  CollisionVolumeFromBody(
                      candidate, definition->bounds.restSizeBlocks));
              return !Owner.CheckBlockCollisionVolume(volume) &&
                     !Owner.CheckCreatureCollisionVolume(
                         volume, creature.GetId());
            });
        if (!safeOrigin ||
            glm::distance(*safeOrigin, creature.GetBodyOrigin()) < 1e-3f)
        {
          return;
        }
        creature.SetBodyOrigin(*safeOrigin);
        if (definition->habitat == CreatureHabitat::Terrestrial)
        {
          SnapCreatureFeetToGround(creature);
        }
      });
  SyncCreatureSpatialIndex();
}

void UWorldEnvironment::TickNaturalPopulation(float dt)
{
  if (Owner.IsPlayerDead() || !CreatureDefinitions)
  {
    NaturalPopulationTimer = 0.f;
    return;
  }
  const UCreature *player = Owner.GetPlayerCreature();
  if (!player)
  {
    return;
  }

  NaturalPopulationTimer += std::max(0.f, dt);
  constexpr float kPopulationIntervalSec = 6.f;
  if (NaturalPopulationTimer < kPopulationIntervalSec)
  {
    return;
  }
  NaturalPopulationTimer = std::fmod(NaturalPopulationTimer,
                                    kPopulationIntervalSec);

  const glm::vec3 playerOrigin = player->GetBodyOrigin();
  const CreaturePopulationCounts currentCounts =
      CountNaturalPopulation(playerOrigin);
  if (!CreaturePopulationRules::CanAddNaturalCreature(
          currentCounts, false, Owner.GetDifficulty(), Owner.GetGameMode()))
  {
    return;
  }
  const WorldDifficultyProfile profile =
      WorldDifficultyProfileFor(Owner.GetDifficulty());
  const bool hostileRoom = CreaturePopulationRules::CanAddNaturalCreature(
      currentCounts, true, Owner.GetDifficulty(), Owner.GetGameMode());

  std::vector<const CreatureDefinition *> spawnable;
  for (const std::string &id : CreatureDefinitions->ListSpawnable())
  {
    spawnable.push_back(CreatureDefinitions->Get(id));
  }
  const auto eligible = SpawnPlanner.BuildEligibleOptions(
      spawnable, hostileRoom, profile.HostileSpawnPressureMultiplier);
  if (eligible.empty() || TotalSpawnOptionWeight(eligible) <= 0.f)
  {
    return;
  }
  TryNaturalPopulationSpawn(playerOrigin, currentCounts, eligible);
}

void UWorldEnvironment::TryNaturalPopulationSpawn(
    const glm::vec3 &playerOrigin, const CreaturePopulationCounts &counts,
    const std::vector<WeightedSpawnOption<const CreatureDefinition *>>
        &eligible)
{
  struct SpawnPlacement
  {
    const CreatureDefinition *definition{nullptr};
    glm::vec3 origin{0.f};
  };
  size_t spawned = 0;
  for (int candidateIndex = 0; candidateIndex < 8 && spawned == 0;
       ++candidateIndex)
  {
    const glm::vec3 candidate =
        SpawnPlanner.NextCandidateOrigin(playerOrigin);
    std::vector<WeightedSpawnOption<SpawnPlacement>> placements;
    placements.reserve(eligible.size());
    for (const WeightedSpawnOption<const CreatureDefinition *> &entry : eligible)
    {
      const CreatureDefinition *definition = entry.value;
      glm::vec3 origin = candidate;
      if (definition->habitat == CreatureHabitat::Terrestrial)
      {
        const int gx = WorldCoordToBlockIndex(origin.x);
        const int gz = WorldCoordToBlockIndex(origin.z);
        const std::optional<float> ground =
            Owner.QueryGroundFeetYColumn(gx, gz);
        if (!ground)
        {
          continue;
        }
        origin.y = *ground;
      }
      placements.push_back({SpawnPlacement{definition, origin}, entry.weight});
    }
    const auto viable = FilterViableSpawnOptions(
        placements, [&](const SpawnPlacement &placement)
        {
          return IsCreatureVolumeLoadedAt(
                     Owner, placement.origin,
                     placement.definition->bounds.restSizeBlocks) &&
                 HabitatAllowsAtForSpawn(
                     Owner, placement.definition->habitat, placement.origin,
                     placement.definition->bounds.restSizeBlocks);
        });
    const float viableWeight = TotalSpawnOptionWeight(viable);
    if (viableWeight <= 0.f)
    {
      continue;
    }
    const auto *choice = ChooseWeightedSpawnOption(
        viable, SpawnPlanner.NextWeightRoll(viableWeight));
    if (!choice)
    {
      continue;
    }
    const CreatureDefinition *selected = choice->value.definition;
    const glm::vec3 spawnOrigin = choice->value.origin;
    const int requestedGroupSize = SpawnPlanner.NextGroupSize(
        selected->catalog.spawnGroupMin, selected->catalog.spawnGroupMax);
    const bool selectedHostile =
        HasCreatureTag(selected->catalog.tags, "hostile");

    for (int groupIndex = 0; groupIndex < requestedGroupSize; ++groupIndex)
    {
      const CreaturePopulationCounts beforeSpawn{
          counts.naturalWorld + spawned,
          counts.local + spawned,
          counts.hostileWorld + (selectedHostile ? spawned : 0),
          counts.hostileLocal + (selectedHostile ? spawned : 0)};
      if (!CreaturePopulationRules::CanAddNaturalCreature(
              beforeSpawn, selectedHostile, Owner.GetDifficulty(),
              Owner.GetGameMode()))
      {
        break;
      }
      glm::vec3 groupOrigin = spawnOrigin;
      if (groupIndex > 0)
      {
        groupOrigin.x += SpawnPlanner.NextGroupJitter();
        groupOrigin.z += SpawnPlanner.NextGroupJitter();
        if (selected->habitat == CreatureHabitat::Terrestrial)
        {
          const std::optional<float> ground = Owner.QueryGroundFeetYColumn(
              WorldCoordToBlockIndex(groupOrigin.x),
              WorldCoordToBlockIndex(groupOrigin.z));
          if (!ground)
          {
            continue;
          }
          groupOrigin.y = *ground;
        }
      }
      const glm::ivec3 groupChunk =
          UChunkManager::WorldToChunk(WorldPosToBlock(groupOrigin));
      if (!Owner.GetBlockWorld().GetChunkManager().HasChunk(groupChunk) ||
          !IsCreatureVolumeLoadedAt(Owner, groupOrigin,
                                    selected->bounds.restSizeBlocks) ||
          !HabitatAllowsAtForSpawn(Owner, selected->habitat, groupOrigin,
                                   selected->bounds.restSizeBlocks))
      {
        continue;
      }
      if (SpawnCreature(selected->Id, groupOrigin, "",
                        CreatureSpawnSource::NaturalPopulation) != 0)
      {
        ++spawned;
      }
    }
  }
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
