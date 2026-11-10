#ifndef IUCREATUREACTIVITYAGENT_H
#define IUCREATUREACTIVITYAGENT_H

#include "Activity/IUCreatureActivitySink.h"
#include "Activity/IUWorldPerception.h"

namespace cutum
{

class IUCreatureActivityAgent
{
public:
  virtual ~IUCreatureActivityAgent() = default;
  virtual const char *GetBehaviorId() const = 0;
  virtual void OnCreatureAdded(CreatureId Id,
                               const CreatureBehaviorParams &behavior) = 0;
  virtual void OnCreatureRemoved(CreatureId Id) = 0;
  virtual void Tick(IUWorldPerception &perception, IUCreatureActivitySink &sink,
                    float dt) = 0;
  /// One member only (the director's per-creature budget under stress);
  /// a creature that is not a member is ignored.
  virtual void TickCreature(CreatureId Id, IUWorldPerception &perception,
                            IUCreatureActivitySink &sink, float dt) = 0;
};

} // namespace cutum

#endif
