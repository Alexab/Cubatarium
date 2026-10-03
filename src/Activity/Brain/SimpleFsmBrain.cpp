#include "Activity/Brain/SimpleFsmBrain.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Creatures/Influence/InfluenceIntentUtil.h"
#include "Game/ModePolicy.h"
#include "Navigation/NavigationTypes.h"
#include "World/Core/World.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum
{

USimpleFsmBrain::USimpleFsmBrain(SimpleFsmMode mode) : Mode(mode) {}

namespace
{

const char *FsmStateName(CreatureFsmState state)
{
  switch (state)
  {
  case CreatureFsmState::Idle:
    return "Idle";
  case CreatureFsmState::Flee:
    return "Flee";
  case CreatureFsmState::Chase:
    return "Chase";
  case CreatureFsmState::Attack:
    return "Attack";
  }
  return "Unknown";
}

void RecordBrainIntent(CreatureId self_id, const CreatureActivityView &view,
                       const CreatureBehaviorSnapshot &snapshot,
                       CreatureFsmState state, const CreatureIntent &intent,
                       const char *reason = nullptr)
{
  if (!UCreatureMovementDiagnostics::IsEnabled())
  {
    return;
  }
  CreatureMovementDiagRecord rec;
  rec.event = "intent";
  rec.creatureId = self_id;
  rec.typeId = view.typeId;
  rec.habitat = ToString(snapshot.habitat);
  rec.behavior = view.behaviorId;
  rec.fsmState = FsmStateName(state);
  rec.body = view.bodyOrigin;
  rec.intentDir = intent.moveDirWorld;
  rec.intentSpeed = intent.moveSpeed;
  rec.activityTick = true;
  if (reason)
  {
    rec.reason = reason;
  }
  UCreatureMovementDiagnostics::Record(rec);
}

glm::vec3 SteerWithFeelersAndSeparation(
    IUWorldPerception &perception, const CreatureActivityView &view,
    const CreatureBehaviorSnapshot &snapshot, const glm::vec3 &raw_dir,
    float separation_weight)
{
  const bool overlapped = !perception.CreaturesClearAt(
      view.bodyOrigin, snapshot.boundsSize, view.Id);
  float sep_weight = separation_weight;
  if (overlapped)
  {
    sep_weight = std::max(sep_weight, 0.85f);
  }
  glm::vec3 move_dir = raw_dir;
  if (!overlapped)
  {
    move_dir = ApplyWallFeelers(perception, snapshot.habitat, view.bodyOrigin,
                                raw_dir, snapshot.boundsSize, view.Id);
  }
  const float sep_radius = SeparationQueryRadius(snapshot.boundsSize);
  const std::vector<CreatureNeighborView> neighbors =
      perception.QueryCreatureNeighborsInRadius(view.bodyOrigin, sep_radius,
                                                view.Id);
  const float min_sep =
      std::max(snapshot.boundsSize.x, snapshot.boundsSize.z) *
      (overlapped ? 1.15f : 0.85f);
  const glm::vec3 separation = ComputeSeparationDirection(
      view.bodyOrigin, snapshot.boundsSize, neighbors, min_sep);
  if (overlapped && glm::length(separation) > 1e-4f)
  {
    // Escape first; chase wish resumes once clear.
    return BlendLocomotionDirection(separation, move_dir, 0.25f);
  }
  return BlendLocomotionDirection(move_dir, separation, sep_weight);
}

float ResolveIntentMoveSpeed(const CreatureBehaviorSnapshot &snapshot)
{
  float speed = snapshot.behavior.moveSpeed;
  if (snapshot.locomotion.walkSpeed > speed)
  {
    speed = snapshot.locomotion.walkSpeed;
  }
  return speed;
}

} // namespace

void USimpleFsmBrain::Tick(UCreatureActivityBlackboard &blackboard,
                           CreatureId self_id, IUWorldPerception &perception,
                           IUCreatureActivitySink &sink, float dt)
{
  const std::optional<CreatureActivityView> view = sink.GetCreatureView(self_id);
  const std::optional<CreatureBehaviorSnapshot> snapshot =
      sink.GetBehaviorSnapshot(self_id);
  if (!view || !snapshot)
  {
    return;
  }

  const std::optional<ControlledCreatureInfo> controlled =
      perception.QueryControlledCreatureInfo();
  CreatureIntent intent;
  intent.clearOnApply = false;

  if (Mode == SimpleFsmMode::Flee)
  {
    if (!controlled)
    {
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "flee_no_controlled");
      return;
    }
    const glm::vec3 controlled_body =
        NavigationGoalBodyFromControlled(*controlled);
    const float dist =
        HorizontalDistanceXZ(view->bodyOrigin, controlled_body);
    if (dist > snapshot->behavior.safeDistance)
    {
      blackboard.state = CreatureFsmState::Idle;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "flee_safe");
      return;
    }
    if (dist <= snapshot->behavior.fleeRadius)
    {
      blackboard.state = CreatureFsmState::Flee;
      blackboard.lastSeenPos = controlled_body;
    }
    if (blackboard.state != CreatureFsmState::Flee)
    {
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "flee_idle");
      return;
    }

    const glm::vec3 flee_dir =
        XzDirectionFromTo(controlled_body, view->bodyOrigin);
    glm::vec3 flee_goal = view->bodyOrigin;
    if (glm::length(flee_dir) > 1e-4f)
    {
      flee_goal = view->bodyOrigin + flee_dir * snapshot->behavior.safeDistance;
      flee_goal.y = view->bodyOrigin.y;
    }
    NavigationQuery query;
    query.search_distance = 32;
    query.body_height = NavigationBodyHeightForBounds(snapshot->boundsSize.y);
    query.max_jump = snapshot->locomotion.jumpHeightBlocks;
    const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
        blackboard.navigation, sink.GetWorld(), view->bodyOrigin, flee_goal,
        query, dt, 0.45f, self_id, view->typeId);
    glm::vec3 move_dir = steer.move_dir;
    if (!steer.has_path || glm::length(move_dir) < 1e-4f)
    {
      if (glm::length(flee_dir) > 1e-4f)
      {
        PickApproachDirection(perception, snapshot->habitat, view->bodyOrigin,
                              flee_dir, snapshot->boundsSize, self_id,
                              move_dir);
      }
      else if (!PickLocomotionDirection(perception, *view, snapshot->habitat,
                                        snapshot->boundsSize, move_dir))
      {
        move_dir = RandomLocomotionDirection(snapshot->habitat);
      }
    }
    move_dir = SteerWithFeelersAndSeparation(perception, *view, *snapshot,
                                             move_dir, 0.5f);
    intent.moveDirWorld = move_dir;
    intent.moveSpeed = ResolveIntentMoveSpeed(*snapshot) *
                       snapshot->behavior.fleeSpeedMultiplier;
    intent.suggestedAnim = LocomotionState::Run;
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      steer.has_path ? "flee_path" : "flee_fallback");
    return;
  }

  if (!controlled)
  {
    // Mob↔mob fallback: chase/attack nearest other creature in aggro.
    CreatureId nearest_id = 0;
    glm::vec3 nearest_body = view->bodyOrigin;
    float nearest_dist = std::numeric_limits<float>::max();
    const auto neighbors = perception.QueryCreatureNeighborsInRadius(
        view->bodyOrigin, snapshot->behavior.aggroRadius, self_id);
    for (const CreatureNeighborView &n : neighbors)
    {
      const float d = HorizontalDistanceXZ(view->bodyOrigin, n.bodyOrigin);
      if (d < nearest_dist)
      {
        nearest_dist = d;
        nearest_id = n.Id;
        nearest_body = n.bodyOrigin;
      }
    }
    if (nearest_id == 0)
    {
      blackboard.state = CreatureFsmState::Idle;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "melee_no_target");
      return;
    }
    blackboard.actionTimer -= dt;
    blackboard.targetId = nearest_id;
    blackboard.lastSeenPos = nearest_body;
    const float dy = nearest_body.y - view->bodyOrigin.y;
    if (nearest_dist <= snapshot->behavior.attackRange && std::abs(dy) <= 1.15f)
    {
      blackboard.state = CreatureFsmState::Attack;
      intent.moveDirWorld = glm::vec3(0.0f);
      intent.moveSpeed = 0.0f;
      SetMeleeInfluenceIntent(intent, nearest_id);
      intent.suggestedAnim = LocomotionState::Action;
      if (blackboard.actionTimer <= 0.0f)
      {
        blackboard.actionTimer = snapshot->behavior.attackCooldown;
      }
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "melee_attack_mob");
      return;
    }
    // Chase nearest (reuse path below via synthetic controlled goal).
    blackboard.state = CreatureFsmState::Chase;
    NavigationQuery query;
    query.search_distance = 32;
    query.body_height = NavigationBodyHeightForBounds(snapshot->boundsSize.y);
    query.max_jump = snapshot->locomotion.jumpHeightBlocks;
    const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
        blackboard.navigation, sink.GetWorld(), view->bodyOrigin, nearest_body,
        query, dt, 0.45f, self_id, view->typeId);
    glm::vec3 move_dir = steer.move_dir;
    if (!steer.has_path || glm::length(move_dir) < 1e-4f)
    {
      const glm::vec3 to_target =
          XzDirectionFromTo(view->bodyOrigin, nearest_body);
      if (!PickApproachDirection(perception, snapshot->habitat,
                                 view->bodyOrigin, to_target,
                                 snapshot->boundsSize, self_id, move_dir))
      {
        intent.moveDirWorld = glm::vec3(0.0f);
        intent.moveSpeed = 0.0f;
        intent.suggestedAnim = LocomotionState::Idle;
        sink.SetIntent(self_id, intent);
        return;
      }
    }
    move_dir = SteerWithFeelersAndSeparation(perception, *view, *snapshot,
                                             move_dir, 0.45f);
    intent.moveDirWorld = move_dir;
    intent.moveSpeed = ResolveIntentMoveSpeed(*snapshot);
    intent.suggestedAnim = LocomotionState::Run;
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      "chase_mob");
    return;
  }

  const glm::vec3 controlled_body =
      NavigationGoalBodyFromControlled(*controlled);
  const float dist =
      HorizontalDistanceXZ(view->bodyOrigin, controlled_body);
  blackboard.actionTimer -= dt;

  {
    const UWorld &world = sink.GetWorld();
    if (!ModePolicy::AllowsHostileAggro(world.GetGameMode(),
                                        world.GetDifficulty()))
    {
      blackboard.state = CreatureFsmState::Idle;
      blackboard.targetId = 0;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "melee_peaceful_idle");
      return;
    }
  }

  if (dist > snapshot->behavior.aggroRadius)
  {
    blackboard.state = CreatureFsmState::Idle;
    blackboard.targetId = 0;
    intent.suggestedAnim = LocomotionState::Idle;
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      "melee_out_of_aggro");
    return;
  }

  blackboard.targetId = controlled->Id;
  blackboard.lastSeenPos = controlled_body;

  const float dy = controlled_body.y - view->bodyOrigin.y;
  // Attack only when close in XZ AND within ~1 block vertically; otherwise keep
  // chasing so mobs climb out of pits / up stairs toward the player.
  if (dist <= snapshot->behavior.attackRange && std::abs(dy) <= 1.15f)
  {
    const UWorld &world = sink.GetWorld();
    if (!ModePolicy::AllowsHostileAggro(world.GetGameMode(),
                                        world.GetDifficulty()))
    {
      blackboard.state = CreatureFsmState::Idle;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "melee_peaceful");
      return;
    }
    blackboard.state = CreatureFsmState::Attack;
    intent.moveDirWorld = glm::vec3(0.0f);
    intent.moveSpeed = 0.0f;
    SetMeleeInfluenceIntent(intent, controlled->Id);
    intent.suggestedAnim = LocomotionState::Action;
    if (blackboard.actionTimer <= 0.0f)
    {
      blackboard.actionTimer = snapshot->behavior.attackCooldown;
    }
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      "melee_attack");
    return;
  }

  blackboard.state = CreatureFsmState::Chase;
  NavigationQuery query;
  query.search_distance = 32;
  query.body_height = NavigationBodyHeightForBounds(snapshot->boundsSize.y);
  query.max_jump = snapshot->locomotion.jumpHeightBlocks;
  const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
      blackboard.navigation, sink.GetWorld(), view->bodyOrigin, controlled_body,
      query, dt, 0.45f, self_id, view->typeId);
  glm::vec3 move_dir = steer.move_dir;
  const char *goal_source = "chase_path";
  if (!steer.has_path || glm::length(move_dir) < 1e-4f)
  {
    // Path follow or approach → feelers (not raw soft seek into walls).
    const glm::vec3 to_player =
        XzDirectionFromTo(view->bodyOrigin, controlled_body);
    if (PickApproachDirection(perception, snapshot->habitat, view->bodyOrigin,
                              to_player, snapshot->boundsSize, self_id,
                              move_dir))
    {
      goal_source =
          steer.has_path ? "chase_path_steer_fallback" : "chase_approach";
    }
    else
    {
      intent.moveDirWorld = glm::vec3(0.0f);
      intent.moveSpeed = 0.0f;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                        "chase_path_fail_idle");
      return;
    }
  }
  else if (steer.partial_path)
  {
    goal_source = "chase_partial";
  }
  move_dir = SteerWithFeelersAndSeparation(perception, *view, *snapshot,
                                           move_dir, 0.45f);
  intent.moveDirWorld = move_dir;
  intent.moveSpeed = ResolveIntentMoveSpeed(*snapshot);
  intent.suggestedAnim = LocomotionState::Run;
  sink.SetIntent(self_id, intent);
  RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                    goal_source);
}

} // namespace cutum
