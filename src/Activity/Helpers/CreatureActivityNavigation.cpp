#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Navigation/UNavigationPathfinder.h"
#include "Navigation/UWaypointFollower.h"
#include "Navigation/WorldNavigationQueries.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"

namespace cutum
{

CreatureNavigationSteerResult SteerCreatureAlongPath(
    CreatureNavigationState &state, const UWorld &world,
    const glm::vec3 &body_origin, const glm::vec3 &goal_body,
    const NavigationQuery &query, float dt, float arrive_radius)
{
  CreatureNavigationSteerResult result;
  state.path_recalc_timer -= dt;
  const bool need_path =
      !state.path.valid || state.path.waypoints.empty() ||
      state.path_recalc_timer <= 0.0f;
  if (need_path)
  {
    const UWorldNavigationQueries navigation(world);
    state.path = UNavigationPathfinder::FindTerrestrialPath(
        navigation, body_origin, goal_body, query);
    state.waypoint_index = 0;
    state.path_recalc_timer = kDefaultPathRecalcInterval;
    if (UCreatureMovementDiagnostics::IsEnabled())
    {
      CreatureMovementDiagRecord rec;
      rec.event = state.path.valid ? "path_ok" : "path_fail";
      rec.body = body_origin;
      rec.pathValid = state.path.valid;
      rec.waypointCount = static_cast<int>(state.path.waypoints.size());
      rec.reason = state.path.valid ? "recalc_ok" : "recalc_fail";
      rec.activityTick = true;
      UCreatureMovementDiagnostics::Record(rec);
    }
  }
  if (!state.path.valid)
  {
    result.path_finished = true;
    return result;
  }
  result.has_path = true;
  const WaypointFollowResult follow = UWaypointFollower::Update(
      body_origin, state.path, arrive_radius, state.waypoint_index);
  result.move_dir = follow.move_dir;
  result.path_finished = follow.path_finished;
  return result;
}

} // namespace cutum
