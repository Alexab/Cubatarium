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

template <typename Ids>
void UFleeActivityAgent::TickIds(const Ids &ids, IUWorldPerception &perception,
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
    Brain->Tick(State[Id], Id, perception, sink, dt);
  }
}

void UFleeActivityAgent::Tick(IUWorldPerception &perception,
                              IUCreatureActivitySink &sink, float dt)
{
  TickIds(Members, perception, sink, dt);
}

void UFleeActivityAgent::TickCreature(CreatureId Id, IUWorldPerception &perception,
                                      IUCreatureActivitySink &sink, float dt)
{
  if (Members.count(Id) != 0)
  {
    const CreatureId one[] = {Id};
    TickIds(one, perception, sink, dt);
  }
}

} // namespace cutum
