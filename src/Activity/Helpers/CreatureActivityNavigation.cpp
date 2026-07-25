#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Creatures/Environment/CreatureTraverseQueries.h"
#include "Navigation/NavigationPathBudget.h"
#include "Navigation/UNavigationPathfinder.h"
#include "Navigation/UWaypointFollower.h"
#include "Navigation/WorldNavigationQueries.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"
#include <algorithm>
#include <optional>

namespace cutum
{
namespace
{

void ResetExhaustBackoff(CreatureNavigationState &state)
{
  state.exhaust_backoff_sec = kPathExhaustBackoffMin;
  state.use_steering_only = false;
}

void ApplyExhaustBackoff(CreatureNavigationState &state)
{
  state.use_steering_only = true;
  state.path_recalc_timer = state.exhaust_backoff_sec;
  state.exhaust_backoff_sec =
      std::min(kPathExhaustBackoffMax, state.exhaust_backoff_sec * 2.0f);
}

bool IsDistantPathLod(const UWorld &world, const glm::vec3 &body_origin)
{
  const std::optional<ControlledCreatureInfo> controlled =
      world.QueryControlledCreatureInfo();
  if (!controlled)
  {
    return false;
  }
  return HorizontalDistanceXZ(body_origin, controlled->bodyOrigin) >
         kPathLodDistanceBlocks;
}

void AdvanceDirectShortcuts(CreatureNavigationState &state,
                            const UWorld &world, const glm::vec3 &body_origin,
                            const NavigationQuery &query, float arrive_radius)
{
  if (!state.path.valid || state.path.waypoints.empty())
  {
    return;
  }
  const glm::vec3 size(0.6f, query.body_height, 0.6f);
  const float max_step = std::max(query.max_jump, 1.0f);
  while (state.waypoint_index + 1 <
         static_cast<int>(state.path.waypoints.size()))
  {
    const int next = state.waypoint_index + 1;
    const glm::vec3 &target =
        state.path.waypoints[static_cast<size_t>(next)].position;
    const float dist = HorizontalDistanceXZ(body_origin, target);
    if (dist <= arrive_radius)
    {
      state.waypoint_index = next;
      continue;
    }
    if (!CanCreatureMoveDirectlyXZ(world, body_origin, target, size, max_step))
    {
      break;
    }
    state.waypoint_index = next;
  }
}

} // namespace

CreatureNavigationSteerResult SteerCreatureAlongPath(
    CreatureNavigationState &state, const UWorld &world,
    const glm::vec3 &body_origin, const glm::vec3 &goal_body,
    const NavigationQuery &query, float dt, float arrive_radius,
    uint64_t creature_id, const std::string &type_id)
{
  CreatureNavigationSteerResult result;
  state.path_recalc_timer -= dt;

  // Stuck → repath (MC checkTimeouts style).
  if (!state.stuck_anchor_valid)
  {
    state.stuck_anchor = body_origin;
    state.stuck_anchor_valid = true;
    state.stuck_timer = 0.0f;
  }
  else if (HorizontalDistanceXZ(body_origin, state.stuck_anchor) >=
           kStuckTravelEpsilon)
  {
    state.stuck_anchor = body_origin;
    state.stuck_timer = 0.0f;
  }
  else
  {
    state.stuck_timer += dt;
    if (state.stuck_timer >= kStuckRepathSeconds)
    {
      // Invalidate corridor but back off — immediate repath storms kill FPS
      // when many mobs are jammed against trees/each other.
      if (state.path.valid)
      {
        state.path.valid = false;
        state.path.waypoints.clear();
        state.waypoint_index = 0;
      }
      state.stuck_timer = 0.0f;
      state.stuck_anchor = body_origin;
      ApplyExhaustBackoff(state);
    }
  }

  const bool distant = IsDistantPathLod(world, body_origin);
  if (!distant && state.use_steering_only && state.path_recalc_timer <= 0.0f)
  {
    state.use_steering_only = false;
  }

  const bool need_path =
      !distant && !state.use_steering_only &&
      (!state.path.valid || state.path.waypoints.empty() ||
       state.path_recalc_timer <= 0.0f);

  if (need_path && UNavigationPathBudget::HasRemainingBudget())
  {
    const UWorldNavigationQueries navigation(world);
    state.path = UNavigationPathfinder::FindTerrestrialPath(
        navigation, body_origin, goal_body, query);
    state.waypoint_index = 0;
    if (state.path.valid)
    {
      ResetExhaustBackoff(state);
      state.path_recalc_timer = kDefaultPathRecalcInterval;
    }
    else if (state.path.failReason == "search_exhausted" ||
             state.path.failReason == "budget_exhausted")
    {
      ApplyExhaustBackoff(state);
    }
    else
    {
      state.path_recalc_timer = kDefaultPathRecalcInterval;
    }
    if (UCreatureMovementDiagnostics::IsEnabled())
    {
      CreatureMovementDiagRecord rec;
      rec.event = state.path.valid ? "path_ok" : "path_fail";
      rec.creatureId = creature_id;
      rec.typeId = type_id;
      rec.body = body_origin;
      rec.pathValid = state.path.valid;
      rec.waypointCount = static_cast<int>(state.path.waypoints.size());
      rec.goalSource = "body";
      if (state.path.valid && state.path.partial)
      {
        rec.reason = "partial";
      }
      else if (state.path.valid)
      {
        rec.reason = "recalc_ok";
      }
      else if (!state.path.failReason.empty())
      {
        rec.reason = state.path.failReason;
      }
      else
      {
        rec.reason = "recalc_fail";
      }
      rec.activityTick = true;
      UCreatureMovementDiagnostics::Record(rec);
    }
  }

  if (!state.path.valid)
  {
    result.path_finished = true;
    return result;
  }

  AdvanceDirectShortcuts(state, world, body_origin, query, arrive_radius);

  result.has_path = true;
  result.partial_path = state.path.partial;
  const WaypointFollowResult follow = UWaypointFollower::Update(
      body_origin, state.path, arrive_radius, state.waypoint_index);
  result.move_dir = follow.move_dir;
  result.path_finished = follow.path_finished;
  return result;
}

} // namespace cutum
