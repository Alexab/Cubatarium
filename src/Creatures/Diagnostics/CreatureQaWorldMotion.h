#pragma once
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureIntent.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace cutum
{
// Explicit QA input driver. It changes only the requested movement of QA NPCs;
// ordinary environment sampling, motor, collision and state derivation run once
// through ExecuteIntent. It never forces facts, clips, position or possession.
inline const std::string &CreatureQaWorldMotionMode()
{
  static const std::string mode = []
  {
    const char *value = std::getenv("CUBA_FLIGHT_QA_WORLD_MOTION");
    return value ? std::string(value) : std::string();
  }();
  return mode;
}
inline bool CreatureQaWorldMotionEnabled()
{
  const auto &mode = CreatureQaWorldMotionMode();
  return mode == "water_pause" || mode == "water_swim_pause" || mode == "land_walk_pause";
}
inline float CreatureQaWorldMoveSpeed()
{
  static const float speed=[]{
    const char *text=std::getenv("CUBA_FLIGHT_QA_WORLD_MOVE_SPEED");
    if(!text)return .5f;
    char *end=nullptr;const float value=std::strtof(text,&end);
    if(end==text || *end!='\0' || !std::isfinite(value) || value<=0.f || value>4.f)
      throw std::runtime_error("Invalid QA world move speed");
    return value;
  }();
  return speed;
}
struct CreatureQaWorldMotionProgress
{
  float elapsed{0.f}; bool move{false}; float direction{1.f};
  bool depthTargetSet{false}, jump{false}; float depthTargetY{0.f};
};
inline std::unordered_map<CreatureId, CreatureQaWorldMotionProgress> &CreatureQaWorldMotionProgressById()
{
  static std::unordered_map<CreatureId, CreatureQaWorldMotionProgress> progress;
  return progress;
}
inline void PrepareCreatureQaWorldMotion(UCreature &creature, float dt)
{
  if (!CreatureQaWorldMotionEnabled() || creature.GetSpawnSource() != CreatureSpawnSource::Qa) return;
  auto &progress = CreatureQaWorldMotionProgressById()[creature.GetId()];
  progress.elapsed += std::max(0.f, dt);
  const float cycle = std::fmod(progress.elapsed, 4.f);
  progress.move = cycle >= .75f && cycle < 3.f;
  progress.direction = static_cast<int>(progress.elapsed / 4.f) % 2 == 0 ? 1.f : -1.f;
  CreatureIntent intent;
  intent.moveDirWorld = progress.move ? glm::vec3(progress.direction, 0.f, 0.f) : glm::vec3(0.f);
  intent.moveSpeed = progress.move ? CreatureQaWorldMoveSpeed() : 0.f;
  if (CreatureQaWorldMotionMode() == "water_swim_pause")
  {
    if (!progress.depthTargetSet)
    {
      progress.depthTargetY = creature.GetBodyOrigin().y;
      progress.depthTargetSet = true;
    }
    // Ordinary jump/swim-up request around the initial submerged depth.
    // Physics retains buoyancy, inertia, collisions and habitat enforcement;
    // neither the actor position nor its derived Swim/Idle facts are assigned.
    progress.jump = creature.GetBodyOrigin().y < progress.depthTargetY - .15f;
    intent.wantJump = progress.jump;
  }
  creature.SetIntent(intent);
}
struct CreatureQaWorldMotionTerrain
{
  bool centerResident{false}, belowResident{false}, centerKnownAir{false}, belowKnownAir{false};
};
inline void RecordCreatureQaWorldMotion(const UCreature &creature, bool bodyBlocked,
    const CreatureQaWorldMotionTerrain &terrain, bool frozen = false)
{
  if (!CreatureQaWorldMotionEnabled() || creature.GetSpawnSource() != CreatureSpawnSource::Qa) return;
  const auto &progress = CreatureQaWorldMotionProgressById()[creature.GetId()];
  const auto &facts = creature.GetLocomotionFacts();
  const auto position = creature.GetBodyOrigin();
  static std::ofstream file("qa_world_motion.jsonl", std::ios::trunc);
  nlohmann::json record = {{"schema", "creature-qa-world-motion.v1"}, {"id", creature.GetId()},
    {"species", creature.GetTypeId()}, {"elapsed", progress.elapsed}, {"requested_move", progress.move},
    {"requested_direction", progress.direction}, {"requested_speed", progress.move ? CreatureQaWorldMoveSpeed() : 0.f},
    {"driver", CreatureQaWorldMotionMode()}, {"requested_jump", progress.jump},
    {"depth_target_y", progress.depthTargetSet ? nlohmann::json(progress.depthTargetY) : nlohmann::json(nullptr)},
    {"position", {position.x, position.y, position.z}}, {"state", ToString(facts.state)},
    {"archetype", ToString(facts.archetype)}, {"in_fluid", facts.inFluid},
    {"on_fluid_bottom", facts.onFluidBottom}, {"on_ground", facts.onGround},
    {"horizontal_speed", facts.horizontalSpeed}, {"vertical_speed", facts.verticalSpeed},
    {"body_blocked", bodyBlocked}, {"frozen", frozen},
    {"legacy_terrain_ready", terrain.centerResident && terrain.belowResident},
    {"center_resident", terrain.centerResident}, {"below_resident", terrain.belowResident},
    {"center_known_air", terrain.centerKnownAir}, {"below_known_air", terrain.belowKnownAir}};
  file << record.dump() << std::endl;
  if (!file) throw std::runtime_error("QA world motion log write failed");
}
}
