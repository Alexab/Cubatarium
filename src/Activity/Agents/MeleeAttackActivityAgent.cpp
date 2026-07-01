#include "Activity/Agents/MeleeAttackActivityAgent.h"

namespace cutum
{

UMeleeAttackActivityAgent::UMeleeAttackActivityAgent()
    : Brain(std::make_unique<USimpleFsmBrain>(SimpleFsmMode::Melee))
{
}

void UMeleeAttackActivityAgent::OnCreatureAdded(CreatureId Id)
{
  Members.insert(Id);
  State[Id] = UCreatureActivityBlackboard{};
}

void UMeleeAttackActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
}

void UMeleeAttackActivityAgent::Tick(IUWorldPerception &perception,
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
