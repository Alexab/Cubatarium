#include "Activity/Brain/SimpleFsmBrain.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Navigation/NavigationTypes.h"
#include <cmath>

namespace cutum
{

USimpleFsmBrain::USimpleFsmBrain(SimpleFsmMode mode) : Mode(mode) {}

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
      return;
    }
    const glm::vec3 delta = view->bodyOrigin - controlled->eyePosition;
    const float dist = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    if (dist > snapshot->behavior.safeDistance)
    {
      blackboard.state = CreatureFsmState::Idle;
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      return;
    }
    if (dist <= snapshot->behavior.fleeRadius)
    {
      blackboard.state = CreatureFsmState::Flee;
      blackboard.lastSeenPos = controlled->eyePosition;
    }
    if (blackboard.state != CreatureFsmState::Flee)
    {
      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(self_id, intent);
      return;
    }

    glm::vec3 flee_goal =
        view->bodyOrigin + glm::normalize(delta) * snapshot->behavior.safeDistance;
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
      if (glm::length(delta) > 1e-4f)
      {
        move_dir = glm::normalize(delta);
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
    return;
  }

  if (!controlled)
  {
    blackboard.state = CreatureFsmState::Idle;
    intent.suggestedAnim = LocomotionState::Idle;
    sink.SetIntent(self_id, intent);
    return;
  }

  const glm::vec3 delta = controlled->eyePosition - view->bodyOrigin;
  const float dist = std::sqrt(delta.x * delta.x + delta.z * delta.z);
  blackboard.actionTimer -= dt;

  if (dist > snapshot->behavior.aggroRadius)
  {
    blackboard.state = CreatureFsmState::Idle;
    blackboard.targetId = 0;
    intent.suggestedAnim = LocomotionState::Idle;
    sink.SetIntent(self_id, intent);
    return;
  }

  blackboard.targetId = controlled->Id;
  blackboard.lastSeenPos = controlled->eyePosition;

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
    return;
  }

  blackboard.state = CreatureFsmState::Chase;
  NavigationQuery query;
  query.search_distance = 32;
  query.body_height = snapshot->boundsSize.y;
  query.max_jump = snapshot->locomotion.jumpHeightBlocks;
  const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
      blackboard.navigation, sink.GetWorld(), view->bodyOrigin,
      controlled->eyePosition, query, dt);
  glm::vec3 move_dir = steer.move_dir;
  if (!steer.has_path || glm::length(move_dir) < 1e-4f)
  {
    move_dir = glm::length(delta) > 1e-4f ? glm::normalize(delta)
                                          : glm::vec3(0.0f);
  }
  intent.moveDirWorld = move_dir;
  intent.moveSpeed = snapshot->behavior.moveSpeed;
  intent.suggestedAnim = LocomotionState::Run;
  sink.SetIntent(self_id, intent);
}

} // namespace cutum
