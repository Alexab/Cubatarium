#ifndef IUAGENTBRAIN_H
#define IUAGENTBRAIN_H

#include "Activity/Brain/CreatureActivityBlackboard.h"
#include "Activity/IUCreatureActivitySink.h"
#include "Activity/IUWorldPerception.h"

namespace cutum
{

class IUAgentBrain
{
public:
  virtual ~IUAgentBrain() = default;
  virtual void Tick(UCreatureActivityBlackboard &blackboard,
                    CreatureId self_id, IUWorldPerception &perception,
                    IUCreatureActivitySink &sink, float dt) = 0;
};

} // namespace cutum

#endif
