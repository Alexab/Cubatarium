#include "Activity/Agents/BotPlayerActivityAgent.h"
#include "Creatures/Influence/InfluenceIntentUtil.h"
#include "Creatures/Core/Creature.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Activity/Helpers/CreatureHostility.h"
#include "Activity/IUWorldPerception.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Navigation/NavigationTypes.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#include <limits>

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

template <typename Ids>
void UBotPlayerActivityAgent::TickIds(const Ids &ids, IUWorldPerception &perception,
                                      IUCreatureActivitySink &sink, float dt)
{
  for (const CreatureId Id : ids)
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
    // Like every other agent: the intent must survive between activity ticks
    // (20 Hz) so the body keeps moving on every world frame; clearOnApply
    // erased it after the first frame (jerky ~1/3 speed, frozen on hitch
    // frames when brains are gated).
    intent.clearOnApply = false;

    const UWorld &world = sink.GetWorld();
    const UCreature *self = world.GetCreature(Id);
    if (!self)
    {
      continue;
    }

    // Prefer the sticky target while it stays a valid hostile in aggro
    // range; otherwise take the nearest (same anti-flap rule as the mob↔mob
    // fallback brain, audit M3).
    CreatureId hostileId = 0;
    float hostileDist = std::numeric_limits<float>::max();
    bool stickyValid = false;
    const auto neighbors = perception.QueryCreatureNeighborsInRadius(
        view->bodyOrigin, behavior.aggroRadius, Id);
    for (const CreatureNeighborView &n : neighbors)
    {
      const float d = HorizontalDistanceXZ(view->bodyOrigin, n.bodyOrigin);
      if (n.Id == bb.targetId)
      {
        if (IsHostileEngagementTarget(world, Id, n.Id))
        {
          stickyValid = true;
          hostileId = n.Id;
          hostileDist = d;
        }
        continue;
      }
      if (!stickyValid && d < hostileDist &&
          IsHostileEngagementTarget(world, Id, n.Id))
      {
        hostileDist = d;
        hostileId = n.Id;
      }
    }
    bb.targetId = hostileId;

    if (hostileId != 0)
    {
      const float attackRange = std::max(1.2f, behavior.attackRange);
      if (hostileDist <= attackRange)
      {
        bb.state = CreatureFsmState::Attack;
        // Strike only off cooldown; attackCooldown from the catalog paces
        // the attacks (the resolver still enforces its own bare-hand
        // interval on top).
        if (bb.actionTimer <= 0.f)
        {
          SetMeleeInfluenceIntent(intent, hostileId);
          intent.suggestedAnim = LocomotionState::Action;
          bb.actionTimer = std::max(0.5f, behavior.attackCooldown);
        }
        sink.SetIntent(Id, intent);
        continue;
      }

      bb.state = CreatureFsmState::Chase;
      const auto bodyOpt = perception.GetCreatureBodyOrigin(hostileId);
      const glm::vec3 goal =
          bodyOpt ? *bodyOpt : view->bodyOrigin;
      NavigationQuery query;
      query.search_distance = 32;
      query.body_height =
          NavigationBodyHeightForBounds(snapshot->boundsSize.y);
      query.body_width = NavigationBodyWidthForBounds(snapshot->boundsSize);
      query.max_jump = snapshot->locomotion.jumpHeightBlocks;
      // Land bots must not path across water (post-motor habitat gate would
      // strand them at the shore).
      query.reject_fluid_nodes =
          snapshot->habitat == CreatureHabitat::Terrestrial;
      const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
          bb.navigation, world, view->bodyOrigin, goal, query, dt, 0.45f, Id,
          view->typeId);
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
        query.body_width = NavigationBodyWidthForBounds(snapshot->boundsSize);
        query.max_jump = snapshot->locomotion.jumpHeightBlocks;
        // Land bots must not path across water (see the chase site above).
        query.reject_fluid_nodes =
            snapshot->habitat == CreatureHabitat::Terrestrial;
        const CreatureNavigationSteerResult steer = SteerCreatureAlongPath(
            bb.navigation, world, view->bodyOrigin, goal, query, dt, 0.45f, Id,
            view->typeId);
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
    if (bb.actionTimer <= 0.f)
    {
      const float span =
          std::max(0.5f, behavior.wanderIntervalMax - behavior.wanderIntervalMin);
      bb.actionTimer = behavior.wanderIntervalMin + span * 0.5f;
      glm::vec3 dir = RandomLocomotionDirection(snapshot->habitat);
      PickLocomotionDirection(perception, *view, snapshot->habitat,
                              snapshot->boundsSize, dir);
      bb.wanderDir = dir;
    }
    glm::vec3 move_dir = bb.wanderDir;
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

void UBotPlayerActivityAgent::Tick(IUWorldPerception &perception,
                                   IUCreatureActivitySink &sink, float dt)
{
  TickIds(Members, perception, sink, dt);
}

void UBotPlayerActivityAgent::TickCreature(CreatureId Id, IUWorldPerception &perception,
                                           IUCreatureActivitySink &sink, float dt)
{
  if (Members.count(Id) != 0)
  {
    const CreatureId one[] = {Id};
    TickIds(one, perception, sink, dt);
  }
}

} // namespace cutum
