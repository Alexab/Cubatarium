#include "Navigation/UWaypointFollower.h"
#include <cmath>

namespace cutum
{

WaypointFollowResult UWaypointFollower::Update(
    const glm::vec3 &current_body_origin, const NavigationPath &path,
    float arrive_radius, int &waypoint_index)
{
  WaypointFollowResult result;
  if (!path.valid || path.waypoints.empty())
  {
    result.path_finished = true;
    return result;
  }
  if (waypoint_index < 0)
  {
    waypoint_index = 0;
  }
  if (waypoint_index >= static_cast<int>(path.waypoints.size()))
  {
    result.path_finished = true;
    return result;
  }

  const glm::vec3 target = path.waypoints[static_cast<size_t>(waypoint_index)]
                               .position;
  glm::vec3 delta = target - current_body_origin;
  delta.y = 0.0f;
  const float dist_sq = glm::dot(delta, delta);
  const float arrive_sq = arrive_radius * arrive_radius;
  if (dist_sq <= arrive_sq)
  {
    ++waypoint_index;
    if (waypoint_index >= static_cast<int>(path.waypoints.size()))
    {
      result.path_finished = true;
      return result;
    }
    const glm::vec3 next_target =
        path.waypoints[static_cast<size_t>(waypoint_index)].position;
    delta = next_target - current_body_origin;
    delta.y = 0.0f;
  }
  if (glm::length(delta) < 1e-4f)
  {
    result.move_dir = glm::vec3(0.0f);
    return result;
  }
  result.move_dir = glm::normalize(delta);
  return result;
}

} // namespace cutum
