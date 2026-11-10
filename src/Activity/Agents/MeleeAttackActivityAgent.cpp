#include "Activity/Agents/MeleeAttackActivityAgent.h"

namespace cutum
{

UMeleeAttackActivityAgent::UMeleeAttackActivityAgent()
    : Brain(std::make_unique<USimpleFsmBrain>(SimpleFsmMode::Melee))
{
}

void UMeleeAttackActivityAgent::OnCreatureAdded(
    CreatureId Id, const CreatureBehaviorParams &behavior)
{
  Members.insert(Id);
  Wander.OnCreatureAdded(Id, behavior);
  State[Id] = UCreatureActivityBlackboard{};
}

void UMeleeAttackActivityAgent::OnCreatureRemoved(CreatureId Id)
{
  Members.erase(Id);
  State.erase(Id);
  Wander.OnCreatureRemoved(Id);
}

template <typename Ids>
void UMeleeAttackActivityAgent::TickIds(const Ids &ids, IUWorldPerception &perception,
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
    UCreatureActivityBlackboard &blackboard = State[Id];
    Brain->Tick(blackboard, Id, perception, sink, dt);
    // Nothing to hunt (no target in aggro range, Creative/Peaceful, no prey):
    // roam like a wanderer instead of standing at the spawn point forever.
    if (blackboard.state == CreatureFsmState::Idle && blackboard.targetId == 0)
    {
      Wander.TickCreature(Id, perception, sink, dt);
    }
  }
}

void UMeleeAttackActivityAgent::Tick(IUWorldPerception &perception,
                                     IUCreatureActivitySink &sink, float dt)
{
  TickIds(Members, perception, sink, dt);
}

void UMeleeAttackActivityAgent::TickCreature(CreatureId Id, IUWorldPerception &perception,
                                             IUCreatureActivitySink &sink, float dt)
{
  if (Members.count(Id) != 0)
  {
    const CreatureId one[] = {Id};
    TickIds(one, perception, sink, dt);
  }
}

} // namespace cutum
