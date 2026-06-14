#include "Activity/Agents/WanderActivityAgent.h"
#include <cmath>
#include <cstdlib>

namespace cutum
{

void UWanderActivityAgent::OnCreatureAdded(CreatureId id)
{
  members_.insert(id);
  ResetWanderState(id, 2.0f, 4.0f);
}

void UWanderActivityAgent::OnCreatureRemoved(CreatureId id)
{
  members_.erase(id);
  State.erase(id);
}

void UWanderActivityAgent::ResetWanderState(CreatureId id, float intervalMin,
                                            float intervalMax)
{
  const float span = intervalMax - intervalMin;
  WanderAgentState &st = State[id];
  st.timer =
      intervalMin + static_cast<float>(std::rand() % 1001) / 1000.0f * span;
}

void UWanderActivityAgent::Tick(IWorldPerception & /*perception*/,
                                ICreatureActivitySink &sink, float dt)
{
  for (const CreatureId id : members_)
  {
    const std::optional<CreatureActivityView> view = sink.GetCreatureView(id);
    if (!view || view->possessed || view->isPlayerCharacter)
    {
      continue;
    }
    const std::optional<CreatureBehaviorSnapshot> snapshot =
        sink.GetBehaviorSnapshot(id);
    if (!snapshot)
    {
      continue;
    }

    WanderAgentState &st = State[id];
    const float intervalMin = snapshot->behavior.wanderIntervalMin;
    const float intervalMax = snapshot->behavior.wanderIntervalMax;
    st.timer -= dt;
    if (st.timer <= 0.0f)
    {
      ResetWanderState(id, intervalMin, intervalMax);
      const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
      st.direction =
          glm::normalize(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
    }

    CreatureIntent intent;
    intent.moveDirWorld = st.direction;
    const float walkSpeed = snapshot->locomotion.walkSpeed;
    intent.moveSpeed =
        walkSpeed > 0.0f ? walkSpeed : snapshot->behavior.moveSpeed;
    intent.clearOnApply = false;
    sink.SetIntent(id, intent);
  }
}

} // namespace cutum
