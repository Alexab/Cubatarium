#ifndef CREATUREACTIVITYDIRECTOR_H
#define CREATUREACTIVITYDIRECTOR_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CreatureActivityTypes.h"
#include "ICreatureActivityAgent.h"
#include "ICreatureActivitySink.h"
#include "IWorldPerception.h"

namespace cutum {

class CreatureActivityDirector {
 public:
 void RegisterAgent(std::unique_ptr<ICreatureActivityAgent> agent);
 void Clear();
 void OnCreatureAdded(CreatureId id, const std::string& behaviorId);
 void OnCreatureRemoved(CreatureId id);
 void TickAgents(IWorldPerception& perception, ICreatureActivitySink& sink, float dt);

 private:
 ICreatureActivityAgent* FindAgent(const std::string& behaviorId) const;

 std::vector<std::unique_ptr<ICreatureActivityAgent>> agents_;
 std::unordered_map<std::string, ICreatureActivityAgent*> agentsByBehaviorId_;
 std::unordered_map<CreatureId, ICreatureActivityAgent*> membership_;
};

} // namespace cutum

#endif
