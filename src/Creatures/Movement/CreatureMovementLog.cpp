#include "Creatures/Movement/CreatureMovementLog.h"

#include "App/Platform/Log.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Movement/CreaturePlacement.h"
#include "World/Core/World.h"

#include <sstream>
#include <unordered_map>
namespace cutum
{

const char *WanderPickFailureLabel(WanderPickFailure reason)
{
  switch (reason)
  {
  case WanderPickFailure::None:
    return "none";
  case WanderPickFailure::HabitatIdle:
    return "habitat_idle";
  case WanderPickFailure::Habitat:
    return "habitat";
  case WanderPickFailure::Collision:
    return "collision";
  }
  return "unknown";
}

const char *HabitatContextLabel(HabitatContext ctx)
{
  switch (ctx)
  {
  case HabitatContext::Spawn:
    return "Spawn";
  case HabitatContext::WanderCurrent:
    return "WanderCurrent";
  case HabitatContext::WanderTarget:
    return "WanderTarget";
  case HabitatContext::MoveApply:
    return "MoveApply";
  }
  return "Unknown";
}

namespace
{

void LogMovement(const char *tag, const std::string &msg)
{
  CubatariumLogInfo(tag, msg);
}

} // namespace

void LogCreatureSpawnOk(CreatureId id, const std::string &speciesId,
                        const std::string &layout)
{
  std::ostringstream oss;
  oss << "SpawnCreature: ok '" << speciesId << "' id=" << id
      << " layout=" << layout;
  LogMovement("Spawn", oss.str());
}

void LogCreatureSpawnFailed(const std::string &speciesId,
                            SpawnFailureReason reason)
{
  std::ostringstream oss;
  oss << "SpawnCreature: failed '" << speciesId << "' reason="
      << SpawnFailureReasonLabel(reason);
  LogMovement("Spawn", oss.str());
}

void LogCreatureSpawnOverlapRejected(const std::string &speciesId)
{
  std::ostringstream oss;
  oss << "SpawnCreature: overlap after placement '" << speciesId << "'";
  LogMovement("Spawn", oss.str());
}

void LogCreatureSpawnBlockedAfterPlacement(const std::string &speciesId,
                                           SpawnFailureReason reason)
{
  std::ostringstream oss;
  oss << "SpawnCreature: blocked after placement '" << speciesId
      << "' reason=" << SpawnFailureReasonLabel(reason);
  LogMovement("Spawn", oss.str());
}

void LogCreatureWanderNoDirection(CreatureId id, const std::string &speciesId,
                                    const glm::vec3 &bodyOrigin,
                                    bool habitatOk, WanderPickFailure reason)
{
  std::ostringstream oss;
  oss << "Wander: no direction id=" << id << " type=" << speciesId << " pos=("
      << bodyOrigin.x << ',' << bodyOrigin.y << ',' << bodyOrigin.z
      << ") habitat_ok=" << (habitatOk ? 1 : 0) << " reason="
      << WanderPickFailureLabel(reason);
  LogMovement("Wander", oss.str());
}

namespace
{

struct MoveProbeLogState
{
  bool blockedGeometry{false};
  bool blockedHabitat{false};
};

std::unordered_map<CreatureId, MoveProbeLogState> gMoveProbeStates;

} // namespace

void LogCreatureMoveProbe(CreatureId id, const UWorld &world,
                          const glm::vec3 &origin, const glm::vec3 &delta,
                          const BodyMoveResult &result, HabitatContext ctx,
                          const EnvironmentSample &originEnv,
                          const FootprintSample &originFootprint)
{
  MoveProbeLogState &prev = gMoveProbeStates[id];
  const bool changed = prev.blockedGeometry != result.blockedGeometry ||
                       prev.blockedHabitat != result.blockedHabitat;
  if (!changed)
  {
    return;
  }
  prev.blockedGeometry = result.blockedGeometry;
  prev.blockedHabitat = result.blockedHabitat;

  std::string speciesId = "unknown";
  if (const UCreature *creature = world.GetCreature(id))
  {
    speciesId = creature->GetTypeId();
  }

  std::ostringstream oss;
  oss << "MoveProbe: id=" << id << " type=" << speciesId
      << " ctx=" << HabitatContextLabel(ctx) << " origin=(" << origin.x << ','
      << origin.y << ',' << origin.z << ") delta=(" << delta.x << ','
      << delta.y << ',' << delta.z << ") movedXZ=" << result.movedXZ
      << " blockedGeometry=" << (result.blockedGeometry ? 1 : 0)
      << " blockedHabitat=" << (result.blockedHabitat ? 1 : 0)
      << " habitatOk=" << (result.habitatOk ? 1 : 0) << " onGround="
      << (result.envAtTarget.onSolidGround ? 1 : 0) << " solidSamples="
      << result.footprintAtTarget.solidSamples << " inOpenAir="
      << (result.envAtTarget.inOpenAir ? 1 : 0) << " originSolid="
      << (originFootprint.solidSamples) << " originOnGround="
      << (originEnv.onSolidGround ? 1 : 0) << " blockHit="
      << (result.envAtTarget.bodyBlocked ? 1 : 0);
  LogMovement("MoveProbe", oss.str());
}

} // namespace cutum
