#ifndef CREATUREACTIVITYDIRECTOR_H
#define CREATUREACTIVITYDIRECTOR_H

#include "Activity/CreatureActivityTypes.h"
#include "Activity/ICreatureActivityAgent.h"
#include "Activity/ICreatureActivitySink.h"
#include "Activity/IWorldPerception.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureActivityDirector
{
public:
  void RegisterAgent(std::unique_ptr<ICreatureActivityAgent> agent);
  void Clear();
  void OnCreatureAdded(CreatureId Id, const std::string &behaviorId);
  void OnCreatureRemoved(CreatureId Id);
  void TickAgents(IWorldPerception &perception, ICreatureActivitySink &sink,
                  float dt);

private:
  ICreatureActivityAgent *FindAgent(const std::string &behaviorId) const;

  std::vector<std::unique_ptr<ICreatureActivityAgent>> Agents;
  std::unordered_map<std::string, ICreatureActivityAgent *> AgentsByBehaviorId;
  std::unordered_map<CreatureId, ICreatureActivityAgent *> Membership;
};

} // namespace cutum

#endif
