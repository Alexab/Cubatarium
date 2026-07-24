#include "Activity/Agents/WanderActivityAgent.h"
#include "Activity/Helpers/CreatureActivitySteering.h"
#include "Creatures/Core/CreatureIntent.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

constexpr float kStuckTimeout = 0.6f;
constexpr float kStuckMinSpeed = 0.05f;
constexpr float kSeparationWeight = 0.5f;
constexpr float kIdleBlockDuration = 0.2f;

void NormalizeWanderIntervalRange(float rawMin, float rawMax, float &outMin,
                                  float &outMax)
{
  constexpr float kDefaultMin = 2.0f;
  constexpr float kDefaultMax = 4.0f;
  outMin = rawMin > 0.0f ? rawMin : kDefaultMin;
  outMax = rawMax > 0.0f ? rawMax : kDefaultMax;
  if (outMax < outMin)
  {
    std::swap(outMin, outMax);
  }
}

namespace
{

void RepickWanderDirection(IUWorldPerception &perception,
                           const CreatureActivityView &view,
                           const CreatureBehaviorSnapshot &snapshot,
                           WanderAgentState &st)
{
  glm::vec3 next_dir = st.direction;
  if (!PickLocomotionDirection(perception, view, snapshot.habitat,
                               snapshot.boundsSize, next_dir))
  {
    next_dir = RandomLocomotionDirection(snapshot.habitat);
  }
  st.direction = next_dir;
}

} // namespace

void UWanderActivityAgent::OnCreatureAdded(
    CreatureId Id, const CreatureBehaviorParams &behavior)
{
  Members.insert(Id);
  WanderAgentState &st = State[Id];
  NormalizeWanderIntervalRange(behavior.wanderIntervalMin,
                               behavior.wanderIntervalMax, st.intervalMin,
                               st.intervalMax);
  ResetWanderState(Id, st.intervalMin, st.intervalMax);
  st.forceRepick = true;
  st.stuckTimer = 0.0f;
  st.idleTimer = 0.0f;
}

void UWanderActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
}

void UWanderActivityAgent::ResetWanderState(CreatureId Id, float intervalMin,
                                            float intervalMax)
{
  const float span = intervalMax - intervalMin;
  WanderAgentState &st = State[Id];
  st.timer =
      intervalMin + static_cast<float>(std::rand() % 1001) / 1000.0f * span;
}

void UWanderActivityAgent::Tick(IUWorldPerception &perception,
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

    WanderAgentState &st = State[Id];
    NormalizeWanderIntervalRange(snapshot->behavior.wanderIntervalMin,
                                 snapshot->behavior.wanderIntervalMax,
                                 st.intervalMin, st.intervalMax);

    const bool volume_blocked = !perception.CreatureVolumeClearAt(
        view->bodyOrigin, snapshot->boundsSize, view->Id);
    if (st.forceRepick || volume_blocked)
    {
      st.forceRepick = false;
      st.timer = 0.0f;
    }

    if (glm::length(st.direction) > 1e-4f &&
        IsLocomotionStuck(st.lastBodyOrigin, view->bodyOrigin, dt,
                          kStuckMinSpeed))
    {
      st.stuckTimer += dt;
    }
    else
    {
      st.stuckTimer = 0.0f;
    }
    if (st.stuckTimer >= kStuckTimeout)
    {
      st.stuckTimer = 0.0f;
      st.timer = 0.0f;
    }

    st.timer -= dt;
    if (st.timer <= 0.0f)
    {
      ResetWanderState(Id, st.intervalMin, st.intervalMax);
      RepickWanderDirection(perception, *view, *snapshot, st);
    }

    const float sep_radius = SeparationQueryRadius(snapshot->boundsSize);
    const std::vector<CreatureNeighborView> neighbors =
        perception.QueryCreatureNeighborsInRadius(view->bodyOrigin, sep_radius,
                                                  view->Id);
    const float min_sep = std::max(snapshot->boundsSize.x, snapshot->boundsSize.z) *
                          0.85f;
    const glm::vec3 separation = ComputeSeparationDirection(
        view->bodyOrigin, snapshot->boundsSize, neighbors, min_sep);
    glm::vec3 move_dir =
        BlendLocomotionDirection(st.direction, separation, kSeparationWeight);

    CreatureIntent intent;
    intent.moveDirWorld = move_dir;
    float move_speed = snapshot->behavior.moveSpeed;
    if (snapshot->habitat == CreatureHabitat::Aerial)
    {
      if (snapshot->locomotion.flySpeed > 0.0f)
      {
        move_speed = snapshot->locomotion.flySpeed;
      }
      else if (snapshot->locomotion.walkSpeed > 0.0f)
      {
        move_speed = snapshot->locomotion.walkSpeed;
      }
    }
    else if (snapshot->locomotion.walkSpeed > 0.0f)
    {
      move_speed = snapshot->locomotion.walkSpeed;
    }

    const glm::vec3 probe = view->bodyOrigin + move_dir * 1.25f;
    const bool forward_clear =
        glm::length(move_dir) < 1e-4f ||
        (perception.CreatureVolumeClearAt(probe, snapshot->boundsSize,
                                          view->Id) &&
         (snapshot->habitat == CreatureHabitat::Aerial ||
          perception.HabitatAllowsMovementAt(snapshot->habitat, probe,
                                             snapshot->boundsSize)));

    if (!forward_clear && glm::length(separation) > 1e-4f)
    {
      move_dir = separation;
      intent.moveDirWorld = move_dir;
    }
    else if (!forward_clear)
    {
      if (glm::length(separation) > 1e-4f)
      {
        st.idleTimer = 0.0f;
        intent.moveDirWorld = separation;
        intent.moveSpeed = move_speed * 0.5f;
        intent.suggestedAnim = LocomotionState::Walk;
        intent.clearOnApply = false;
        sink.SetIntent(Id, intent);
        st.lastBodyOrigin = view->bodyOrigin;
        continue;
      }
      st.idleTimer += dt;
      if (st.idleTimer >= kIdleBlockDuration)
      {
        st.idleTimer = 0.0f;
        st.timer = 0.0f;
      }
      intent.moveDirWorld = glm::vec3(0.0f);
      intent.moveSpeed = 0.0f;
      intent.suggestedAnim = LocomotionState::Idle;
      intent.clearOnApply = false;
      sink.SetIntent(Id, intent);
      st.lastBodyOrigin = view->bodyOrigin;
      continue;
    }
    else
    {
      st.idleTimer = 0.0f;
    }

    intent.moveSpeed = move_speed;
    if (glm::length(move_dir) > 1e-4f)
    {
      if (snapshot->habitat == CreatureHabitat::Aerial)
      {
        intent.suggestedAnim = LocomotionState::Fly;
      }
      else
      {
        intent.suggestedAnim = LocomotionState::Walk;
      }
    }
    intent.clearOnApply = false;
    sink.SetIntent(Id, intent);
    st.lastBodyOrigin = view->bodyOrigin;
  }
}

} // namespace cutum
