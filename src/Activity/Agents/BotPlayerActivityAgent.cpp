#include "Activity/Agents/BotPlayerActivityAgent.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Activity/IUWorldPerception.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Navigation/NavigationTypes.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float BotMoveSpeed(const CreatureBehaviorSnapshot &snapshot)
{
  float speed = snapshot.behavior.moveSpeed;
  if (snapshot.locomotion.walkSpeed > speed)
  {
    speed = snapshot.locomotion.walkSpeed;
  }
  return speed;
}

} // namespace

void UBotPlayerActivityAgent::OnCreatureAdded(
    CreatureId Id, const CreatureBehaviorParams &behavior)
{
  Members.insert(Id);
  State[Id] = UCreatureActivityBlackboard{};
  Behaviors[Id] = behavior;
}

void UBotPlayerActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
  Behaviors.erase(Id);
}

void UBotPlayerActivityAgent::Tick(IUWorldPerception &perception,
                                   IUCreatureActivitySink &sink, float dt)
{
  for (const CreatureId Id : Members)
  {
    const std::optional<CreatureActivityView> view = sink.GetCreatureView(Id);
    if (!view || view->possessed || view->isPlayerCharacter)
    {
      continue;
    }
    if (!perception.IsWithinActivityRange(view->bodyOrigin))
    {
      continue;
    }
    const std::optional<CreatureBehaviorSnapshot> snapshot =
        sink.GetBehaviorSnapshot(Id);
    if (!snapshot)
    {
      continue;
    }

    CreatureBehaviorParams behavior =
        Behaviors.count(Id) ? Behaviors[Id] : snapshot->behavior;
    behavior.aggroRadius = snapshot->behavior.aggroRadius;

    UCreatureActivityBlackboard &bb = State[Id];
    bb.actionTimer -= dt;

    CreatureIntent intent{};
    intent.clearOnApply = true;

    const std::optional<ControlledCreatureInfo> controlled =
        perception.QueryControlledCreatureInfo();
    if (controlled)
    {
      const glm::vec3 goal = NavigationGoalBodyFromControlled(*controlled);
      const float dist = HorizontalDistanceXZ(view->bodyOrigin, goal);
      const float followStart = std::max(3.5f, behavior.aggroRadius * 0.35f);

      if (dist > followStart)
      {
        NavigationQuery query;
        query.search_distance = 32;
        query.body_height =
            NavigationBodyHeightForBounds(snapshot->boundsSize.y);
        query.max_jump = snapshot->locomotion.jumpHeightBlocks;
        const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
            bb.navigation, sink.GetWorld(), view->bodyOrigin, goal, query, dt,
            0.45f, Id, view->typeId);
        glm::vec3 move_dir = steer.move_dir;
        if (glm::length(move_dir) < 1e-4f)
        {
          move_dir = XzDirectionFromTo(view->bodyOrigin, goal);
          PickApproachDirection(perception, snapshot->habitat, view->bodyOrigin,
                                move_dir, snapshot->boundsSize, Id, move_dir);
        }
        move_dir = ApplyWallFeelers(perception, snapshot->habitat,
                                    view->bodyOrigin, move_dir,
                                    snapshot->boundsSize, Id);
        intent.moveDirWorld = move_dir;
        intent.moveSpeed = BotMoveSpeed(*snapshot);
        intent.suggestedAnim = LocomotionState::Run;
        sink.SetIntent(Id, intent);
        continue;
      }

      intent.suggestedAnim = LocomotionState::Idle;
      sink.SetIntent(Id, intent);
      continue;
    }

    // No controlled player: light wander.
    bb.actionTimer -= dt;
    if (bb.actionTimer <= 0.f)
    {
      const float span =
          std::max(0.5f, behavior.wanderIntervalMax - behavior.wanderIntervalMin);
      bb.actionTimer = behavior.wanderIntervalMin + span * 0.5f;
      glm::vec3 dir = RandomLocomotionDirection(snapshot->habitat);
      PickLocomotionDirection(perception, *view, snapshot->habitat,
                              snapshot->boundsSize, dir);
      bb.lastSeenPos = dir;
    }
    glm::vec3 move_dir = bb.lastSeenPos;
    if (glm::length(move_dir) < 1e-4f)
    {
      move_dir = RandomLocomotionDirection(snapshot->habitat);
    }
    move_dir = ApplyWallFeelers(perception, snapshot->habitat, view->bodyOrigin,
                                move_dir, snapshot->boundsSize, Id);
    intent.moveDirWorld = move_dir;
    intent.moveSpeed = BotMoveSpeed(*snapshot) * 0.7f;
    intent.suggestedAnim = LocomotionState::Walk;
    sink.SetIntent(Id, intent);
  }
}

} // namespace cutum
