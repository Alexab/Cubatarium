#ifndef FLEEACTIVITYAGENT_H
#define FLEEACTIVITYAGENT_H

#include "Activity/Brain/CreatureActivityBlackboard.h"
#include "Activity/Brain/SimpleFsmBrain.h"
#include "Activity/IUCreatureActivityAgent.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

class UFleeActivityAgent : public IUCreatureActivityAgent
{
public:
  UFleeActivityAgent();

  const char *GetBehaviorId() const override { return "flee"; }
  void OnCreatureAdded(CreatureId Id) override;
  void OnCreatureRemoved(CreatureId Id) override;
  void Tick(IUWorldPerception &perception, IUCreatureActivitySink &sink,
            float dt) override;

private:
  std::unordered_set<CreatureId> Members;
  std::unordered_map<CreatureId, UCreatureActivityBlackboard> State;
  std::unique_ptr<IUAgentBrain> Brain;
};

} // namespace cutum

#endif
