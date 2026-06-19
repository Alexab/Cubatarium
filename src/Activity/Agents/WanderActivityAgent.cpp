#include "Activity/Agents/WanderActivityAgent.h"
#include <cmath>
#include <cstdlib>

namespace cutum
{

namespace
{

constexpr int kMaxDirectionAttempts = 8;
constexpr float kWanderProbeDistance = 1.0f;

glm::vec3 RandomWanderDirection(CreatureHabitat habitat)
{
  const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
  glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
  if (habitat == CreatureHabitat::Aquatic ||
      habitat == CreatureHabitat::Amphibious)
  {
    const float pitch =
        static_cast<float>((std::rand() % 201) - 100) / 100.0f * 0.45f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Lava)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.25f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Aerial)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.35f;
    dir.y = pitch;
  }
  if (glm::length(dir) > 1e-4f)
  {
    dir = glm::normalize(dir);
  }
  return dir;
}

bool PickWanderDirection(IWorldPerception &perception,
                         const CreatureActivityView &view,
                         CreatureHabitat habitat, const glm::vec3 &boundsSize,
                         glm::vec3 &outDirection)
{
  for (int attempt = 0; attempt < kMaxDirectionAttempts; ++attempt)
  {
    const glm::vec3 dir = RandomWanderDirection(habitat);
    const glm::vec3 probe =
        view.bodyOrigin + dir * kWanderProbeDistance;
    if (perception.CanCreatureOccupyAt(habitat, probe, boundsSize))
    {
      outDirection = dir;
      return true;
    }
  }
  return false;
}

} // namespace

void UWanderActivityAgent::OnCreatureAdded(CreatureId Id)
{
  Members.insert(Id);
  ResetWanderState(Id, 2.0f, 4.0f);
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

void UWanderActivityAgent::Tick(IWorldPerception &perception,
                                ICreatureActivitySink &sink, float dt)
{
  for (const CreatureId Id : Members)
  {
    const std::optional<CreatureActivityView> view = sink.GetCreatureView(Id);
    if (!view || view->possessed || view->isPlayerCharacter)
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
    const float intervalMin = snapshot->behavior.wanderIntervalMin;
    const float intervalMax = snapshot->behavior.wanderIntervalMax;
    st.timer -= dt;
    if (st.timer <= 0.0f)
    {
      ResetWanderState(Id, intervalMin, intervalMax);
      glm::vec3 nextDir = st.direction;
      if (!PickWanderDirection(perception, *view, snapshot->habitat,
                               snapshot->boundsSize, nextDir))
      {
        nextDir = glm::vec3(0.0f);
      }
      st.direction = nextDir;
    }

    CreatureIntent intent;
    intent.moveDirWorld = st.direction;
    const float walkSpeed = snapshot->locomotion.walkSpeed;
    intent.moveSpeed =
        walkSpeed > 0.0f ? walkSpeed : snapshot->behavior.moveSpeed;
    intent.clearOnApply = false;
    sink.SetIntent(Id, intent);
  }
}

} // namespace cutum
