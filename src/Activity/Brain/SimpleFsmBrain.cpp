#include "Activity/Brain/SimpleFsmBrain.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Navigation/NavigationTypes.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"
#include <cmath>

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
    query.body_height = snapshot->boundsSize.y;
    query.max_jump = snapshot->locomotion.jumpHeightBlocks;
    const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
        blackboard.navigation, sink.GetWorld(), view->bodyOrigin, flee_goal,
        query, dt);
    glm::vec3 move_dir = steer.move_dir;
    if (!steer.has_path || glm::length(move_dir) < 1e-4f)
    {
      if (glm::length(flee_dir) > 1e-4f)
      {
        move_dir = flee_dir;
      }
      else if (!PickLocomotionDirection(perception, *view, snapshot->habitat,
                                        snapshot->boundsSize, move_dir))
      {
        move_dir = RandomLocomotionDirection(snapshot->habitat);
      }
    }
    constexpr float kFleeSeparationWeight = 0.5f;
    const float sep_radius = SeparationQueryRadius(snapshot->boundsSize);
    const std::vector<CreatureNeighborView> neighbors =
        perception.QueryCreatureNeighborsInRadius(view->bodyOrigin, sep_radius,
                                                  self_id);
    const float min_sep = std::max(snapshot->boundsSize.x, snapshot->boundsSize.z) *
                          0.85f;
    const glm::vec3 separation = ComputeSeparationDirection(
        view->bodyOrigin, snapshot->boundsSize, neighbors, min_sep);
    move_dir = BlendLocomotionDirection(move_dir, separation, kFleeSeparationWeight);
    intent.moveDirWorld = move_dir;
    intent.moveSpeed = snapshot->behavior.moveSpeed *
                       snapshot->behavior.fleeSpeedMultiplier;
    intent.suggestedAnim = LocomotionState::Run;
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      steer.has_path ? "flee_path" : "flee_fallback");
    return;
  }

  if (!controlled)
  {
    blackboard.state = CreatureFsmState::Idle;
    intent.suggestedAnim = LocomotionState::Idle;
    sink.SetIntent(self_id, intent);
    RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                      "melee_no_controlled");
    return;
  }

  const glm::vec3 controlled_body =
      NavigationGoalBodyFromControlled(*controlled);
  const float dist =
      HorizontalDistanceXZ(view->bodyOrigin, controlled_body);
  blackboard.actionTimer -= dt;

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

  if (dist <= snapshot->behavior.attackRange)
  {
    blackboard.state = CreatureFsmState::Attack;
    intent.moveDirWorld = glm::vec3(0.0f);
    intent.moveSpeed = 0.0f;
    intent.attackTargetId = controlled->Id;
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
  query.body_height = snapshot->boundsSize.y;
  query.max_jump = snapshot->locomotion.jumpHeightBlocks;
  const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
      blackboard.navigation, sink.GetWorld(), view->bodyOrigin, controlled_body,
      query, dt);
  glm::vec3 move_dir = steer.move_dir;
  if (!steer.has_path || glm::length(move_dir) < 1e-4f)
  {
    move_dir = XzDirectionFromTo(view->bodyOrigin, controlled_body);
  }
  intent.moveDirWorld = move_dir;
  intent.moveSpeed = snapshot->behavior.moveSpeed;
  intent.suggestedAnim = LocomotionState::Run;
  sink.SetIntent(self_id, intent);
  RecordBrainIntent(self_id, *view, *snapshot, blackboard.state, intent,
                    steer.has_path ? "chase_path" : "chase_fallback");
}

} // namespace cutum
