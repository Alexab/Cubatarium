#include "CreatureActivityDirector.h"

namespace cutum {

void UCreatureActivityDirector::RegisterAgent(std::unique_ptr<ICreatureActivityAgent> agent)
{
 if (!agent) {
  return;
 }
 agentsByBehaviorId_[agent->GetBehaviorId()] = agent.get();
 agents_.push_back(std::move(agent));
}

void UCreatureActivityDirector::Clear()
{
 std::vector<CreatureId> ids;
 ids.reserve(membership_.size());
 for (const auto& entry : membership_) {
  ids.push_back(entry.first);
 }
 for (const CreatureId id : ids) {
  OnCreatureRemoved(id);
 }
}

void UCreatureActivityDirector::OnCreatureAdded(CreatureId id, const std::string& behaviorId)
{
 if (id == 0 || behaviorId.empty() || behaviorId == "none") {
  return;
 }
 ICreatureActivityAgent* agent = FindAgent(behaviorId);
 if (!agent) {
  return;
 }
 OnCreatureRemoved(id);
 agent->OnCreatureAdded(id);
 membership_[id] = agent;
}

void UCreatureActivityDirector::OnCreatureRemoved(CreatureId id)
{
 const auto it = membership_.find(id);
 if (it == membership_.end()) {
  return;
 }
 if (it->second) {
  it->second->OnCreatureRemoved(id);
 }
 membership_.erase(it);
}

void UCreatureActivityDirector::TickAgents(IWorldPerception& perception,
                                          ICreatureActivitySink& sink, float dt)
{
 for (const auto& agent : agents_) {
  agent->Tick(perception, sink, dt);
 }
}

ICreatureActivityAgent* UCreatureActivityDirector::FindAgent(const std::string& behaviorId) const
{
 const auto it = agentsByBehaviorId_.find(behaviorId);
 return it != agentsByBehaviorId_.end() ? it->second : nullptr;
}

} // namespace cutum
