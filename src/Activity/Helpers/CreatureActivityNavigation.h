#ifndef CREATUREACTIVITYNAVIGATION_H
#define CREATUREACTIVITYNAVIGATION_H

#include "Activity/CreatureActivityTypes.h"
#include "Navigation/NavigationTypes.h"
#include "World/Core/World.h"
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

struct CreatureNavigationState
{
  NavigationPath path;
  int waypoint_index{0};
  float path_recalc_timer{0.0f};
  /// After search_exhausted: exponential 0.5→2→4s before next A*.
  float exhaust_backoff_sec{0.5f};
  bool use_steering_only{false};
  glm::vec3 stuck_anchor{0.0f};
  float stuck_timer{0.0f};
  bool stuck_anchor_valid{false};
};

struct CreatureNavigationSteerResult
{
  glm::vec3 move_dir{0.0f};
  bool has_path{false};
  bool path_finished{false};
  bool partial_path{false};
};

constexpr float kDefaultPathRecalcInterval = 1.0f;
constexpr float kPathExhaustBackoffMin = 0.75f;
constexpr float kPathExhaustBackoffMax = 6.0f;
constexpr float kPathLodDistanceBlocks = 48.0f;
constexpr float kStuckRepathSeconds = 2.5f;
constexpr float kStuckTravelEpsilon = 0.25f;

/// A* goals must use feet/body, never eye (stand-node at eye Y fails clearance).
inline glm::vec3 NavigationGoalBodyFromControlled(
    const ControlledCreatureInfo &controlled)
{
  return controlled.bodyOrigin;
}

inline float HorizontalDistanceXZ(const glm::vec3 &a, const glm::vec3 &b)
{
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}

inline glm::vec3 XzDirectionFromTo(const glm::vec3 &from, const glm::vec3 &to)
{
  glm::vec3 dir(to.x - from.x, 0.0f, to.z - from.z);
  const float len = glm::length(dir);
  if (len < 1e-4f)
  {
    return glm::vec3(0.0f);
  }
  return dir / len;
}

CreatureNavigationSteerResult SteerCreatureAlongPath(
    CreatureNavigationState &state, const UWorld &world,
    const glm::vec3 &body_origin, const glm::vec3 &goal_body,
    const NavigationQuery &query, float dt, float arrive_radius = 0.45f,
    uint64_t creature_id = 0, const std::string &type_id = {});

} // namespace cutum

#endif
