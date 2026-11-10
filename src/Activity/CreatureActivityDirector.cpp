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
  StressBudget.Remove(Id);
}

void UCreatureActivityDirector::TickAgents(IUWorldPerception &perception,
                                           IUCreatureActivitySink &sink,
                                           float dt, bool stress_tick)
{
  LastAgentsTicked = 0;
  LastAgentsDeferred = 0;
  AccumulatedTickDt += dt;
  if (AccumulatedTickDt < ActivityTickInterval)
  {
    return;
  }
  const float cognitive_dt =
      std::min(AccumulatedTickDt, kMaxCognitiveDt);
  AccumulatedTickDt -= cognitive_dt;
  UNavigationPathBudget::BeginActivityTick();
  const size_t agent_count = Agents.size();
  if (agent_count == 0)
  {
    return;
  }
  if (stress_tick)
  {
    // Budget creatures, not behavior types (see UCreatureTickBudget).
    std::vector<CreatureId> ids;
    ids.reserve(Membership.size());
    for (const auto &entry : Membership)
    {
      if (entry.second)
      {
        ids.push_back(entry.first);
      }
    }
    std::sort(ids.begin(), ids.end());
    const auto slice = StressBudget.Select(
        ids, cognitive_dt, UCreatureTickBudget::BudgetFor(ids.size()));
    for (const auto &[Id, creatureDt] : slice)
    {
      const auto it = Membership.find(Id);
      if (it != Membership.end() && it->second)
      {
        it->second->TickCreature(Id, perception, sink, creatureDt);
        ++LastAgentsTicked;
      }
    }
    LastAgentsDeferred = static_cast<int>(ids.size() - slice.size());
    return;
  }
  StressBudget.Reset();
  for (const auto &agent : Agents)
  {
    agent->Tick(perception, sink, cognitive_dt);
    ++LastAgentsTicked;
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
