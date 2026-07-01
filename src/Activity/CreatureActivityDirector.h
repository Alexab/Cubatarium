#ifndef CREATUREACTIVITYDIRECTOR_H
#define CREATUREACTIVITYDIRECTOR_H

#include "Activity/CreatureActivityTypes.h"
#include "Activity/IUCreatureActivityAgent.h"
#include "Activity/IUCreatureActivitySink.h"
#include "Activity/IUWorldPerception.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UCreatureActivityDirector
{
public:
  void RegisterAgent(std::unique_ptr<IUCreatureActivityAgent> agent);
  void Clear();
  void OnCreatureAdded(CreatureId Id, const std::string &behaviorId);
  void OnCreatureRemoved(CreatureId Id);
  void TickAgents(IUWorldPerception &perception, IUCreatureActivitySink &sink,
                  float dt);

private:
  IUCreatureActivityAgent *FindAgent(const std::string &behaviorId) const;

  std::vector<std::unique_ptr<IUCreatureActivityAgent>> Agents;
  std::unordered_map<std::string, IUCreatureActivityAgent *> AgentsByBehaviorId;
  std::unordered_map<CreatureId, IUCreatureActivityAgent *> Membership;
};

} // namespace cutum

#endif
