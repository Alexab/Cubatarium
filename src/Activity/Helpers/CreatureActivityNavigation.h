#ifndef CREATUREACTIVITYNAVIGATION_H
#define CREATUREACTIVITYNAVIGATION_H

#include "Activity/CreatureActivityTypes.h"
#include "Navigation/NavigationTypes.h"
#include "World/Core/World.h"
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

struct CreatureNavigationState
{
  NavigationPath path;
  int waypoint_index{0};
  float path_recalc_timer{0.0f};
};

struct CreatureNavigationSteerResult
{
  glm::vec3 move_dir{0.0f};
  bool has_path{false};
  bool path_finished{false};
};

constexpr float kDefaultPathRecalcInterval = 0.5f;

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
    const NavigationQuery &query, float dt, float arrive_radius = 0.45f);

} // namespace cutum

#endif
