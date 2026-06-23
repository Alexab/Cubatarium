#include "Activity/CreatureActivityDirector.h"

namespace cutum
{

void UCreatureActivityDirector::RegisterAgent(
    std::unique_ptr<ICreatureActivityAgent> agent)
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
                                                const std::string &behaviorId)
{
  if (Id == 0 || behaviorId.empty() || behaviorId == "none")
  {
    return;
  }
  ICreatureActivityAgent *agent = FindAgent(behaviorId);
  if (!agent)
  {
    return;
  }
  OnCreatureRemoved(Id);
  agent->OnCreatureAdded(Id);
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

void UCreatureActivityDirector::TickAgents(IWorldPerception &perception,
                                           ICreatureActivitySink &sink,
                                           float dt)
{
  for (const auto &agent : Agents)
  {
    agent->Tick(perception, sink, dt);
  }
}

ICreatureActivityAgent *
UCreatureActivityDirector::FindAgent(const std::string &behaviorId) const
{
  const auto it = AgentsByBehaviorId.find(behaviorId);
  return it != AgentsByBehaviorId.end() ? it->second : nullptr;
}

} // namespace cutum
