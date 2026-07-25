#include "Activity/CreatureActivityDirector.h"
#include "Navigation/NavigationPathBudget.h"
#include <algorithm>

namespace cutum
{

void UCreatureActivityDirector::RegisterAgent(
    std::unique_ptr<IUCreatureActivityAgent> agent)
{
  if (!agent)
  {
    return;
  }
  AgentsByBehaviorId[agent->GetBehaviorId()] = agent.get();
  Agents.push_back(std::move(agent));
}

void UCreatureActivityDirector::Clear()
{
  std::vector<CreatureId> ids;
  ids.reserve(Membership.size());
  for (const auto &entry : Membership)
  {
    ids.push_back(entry.first);
  }
  for (const CreatureId Id : ids)
  {
    OnCreatureRemoved(Id);
  }
}

void UCreatureActivityDirector::OnCreatureAdded(CreatureId Id,
                                                const std::string &behaviorId,
                                                const CreatureBehaviorParams
                                                    *behavior)
{
  if (Id == 0 || behaviorId.empty() || behaviorId == "none")
  {
    return;
  }
  IUCreatureActivityAgent *agent = FindAgent(behaviorId);
  if (!agent)
  {
    return;
  }
  OnCreatureRemoved(Id);
  const CreatureBehaviorParams emptyBehavior{};
  agent->OnCreatureAdded(Id, behavior ? *behavior : emptyBehavior);
  Membership[Id] = agent;
}

void UCreatureActivityDirector::OnCreatureRemoved(CreatureId Id)
{
  const auto it = Membership.find(Id);
  if (it == Membership.end())
  {
    return;
  }
  if (it->second)
  {
    it->second->OnCreatureRemoved(Id);
  }
  Membership.erase(it);
}

void UCreatureActivityDirector::TickAgents(IUWorldPerception &perception,
                                           IUCreatureActivitySink &sink,
                                           float dt)
{
  AccumulatedTickDt += dt;
  if (AccumulatedTickDt < ActivityTickInterval)
  {
    return;
  }
  const float cognitive_dt = AccumulatedTickDt;
  AccumulatedTickDt = 0.0f;
  UNavigationPathBudget::BeginActivityTick();
  for (const auto &agent : Agents)
  {
    agent->Tick(perception, sink, cognitive_dt);
  }
}

void UCreatureActivityDirector::SetActivityTickInterval(float seconds)
{
  ActivityTickInterval = std::max(seconds, 0.01f);
}

IUCreatureActivityAgent *
UCreatureActivityDirector::FindAgent(const std::string &behaviorId) const
{
  const auto it = AgentsByBehaviorId.find(behaviorId);
  return it != AgentsByBehaviorId.end() ? it->second : nullptr;
}

} // namespace cutum
