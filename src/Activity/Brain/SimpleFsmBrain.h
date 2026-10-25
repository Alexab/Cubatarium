#ifndef SIMPLEFSMBRAIN_H
#define SIMPLEFSMBRAIN_H

#include "Activity/Brain/IUAgentBrain.h"
#include "Creatures/Core/CreatureCatalogTypes.h"

namespace cutum
{

enum class SimpleFsmMode : uint8_t
{
  Flee,
  Melee
};

class USimpleFsmBrain : public IUAgentBrain
{
public:
  explicit USimpleFsmBrain(SimpleFsmMode mode);

  void Tick(UCreatureActivityBlackboard &blackboard, CreatureId self_id,
            IUWorldPerception &perception, IUCreatureActivitySink &sink,
            float dt) override;

private:
  // Mode handlers (audit 4.5): Tick used to run Flee and both Melee paths
  // in one 330-line function.
  void TickFlee(UCreatureActivityBlackboard &blackboard, CreatureId self_id,
                IUWorldPerception &perception, IUCreatureActivitySink &sink,
                float dt, const CreatureActivityView &view,
                const CreatureBehaviorSnapshot &snapshot);
  void TickMeleeFallback(UCreatureActivityBlackboard &blackboard,
                         CreatureId self_id, IUWorldPerception &perception,
                         IUCreatureActivitySink &sink, float dt,
                         const CreatureActivityView &view,
                         const CreatureBehaviorSnapshot &snapshot);
  void TickMeleeAtControlled(UCreatureActivityBlackboard &blackboard,
                             CreatureId self_id, IUWorldPerception &perception,
                             IUCreatureActivitySink &sink, float dt,
                             const CreatureActivityView &view,
                             const CreatureBehaviorSnapshot &snapshot);

  SimpleFsmMode Mode;
};

} // namespace cutum

#endif
