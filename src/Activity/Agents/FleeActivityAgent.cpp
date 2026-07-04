#include "Activity/Agents/FleeActivityAgent.h"

namespace cutum
{

UFleeActivityAgent::UFleeActivityAgent()
    : Brain(std::make_unique<USimpleFsmBrain>(SimpleFsmMode::Flee))
{
}

void UFleeActivityAgent::OnCreatureAdded(
    CreatureId Id, const CreatureBehaviorParams &behavior)
{
  (void)behavior;
  Members.insert(Id);
  State[Id] = UCreatureActivityBlackboard{};
}

void UFleeActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
}

void UFleeActivityAgent::Tick(IUWorldPerception &perception,
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
    Brain->Tick(State[Id], Id, perception, sink, dt);
  }
}

} // namespace cutum
