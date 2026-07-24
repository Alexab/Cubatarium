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
  SimpleFsmMode Mode;
};

} // namespace cutum

#endif
