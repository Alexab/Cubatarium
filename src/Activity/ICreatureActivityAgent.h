#ifndef ICREATUREACTIVITYAGENT_H
#define ICREATUREACTIVITYAGENT_H

#include "Activity/ICreatureActivitySink.h"
#include "Activity/IWorldPerception.h"

namespace cutum
{

class ICreatureActivityAgent
{
public:
  virtual ~ICreatureActivityAgent() = default;
  virtual const char *GetBehaviorId() const = 0;
  virtual void OnCreatureAdded(CreatureId Id) = 0;
  virtual void OnCreatureRemoved(CreatureId Id) = 0;
  virtual void Tick(IWorldPerception &perception, ICreatureActivitySink &sink,
                    float dt) = 0;
};

} // namespace cutum

#endif
