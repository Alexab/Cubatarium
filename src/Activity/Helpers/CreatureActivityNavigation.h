#ifndef CREATUREACTIVITYNAVIGATION_H
#define CREATUREACTIVITYNAVIGATION_H

#include "Navigation/NavigationTypes.h"
#include "World/Core/World.h"
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

CreatureNavigationSteerResult SteerCreatureAlongPath(
    CreatureNavigationState &state, const UWorld &world,
    const glm::vec3 &body_origin, const glm::vec3 &goal_body,
    const NavigationQuery &query, float dt, float arrive_radius = 0.45f);

} // namespace cutum

#endif
