#ifndef WANDERACTIVITYAGENT_H
#define WANDERACTIVITYAGENT_H

#include "activity/CreatureActivityTypes.h"
#include "activity/ICreatureActivityAgent.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

struct WanderAgentState
{
  float timer{0.0f};
  glm::vec3 direction{1.0f, 0.0f, 0.0f};
};

class UWanderActivityAgent : public ICreatureActivityAgent
{
public:
  const char *GetBehaviorId() const override { return "wander"; }
  void OnCreatureAdded(CreatureId id) override;
  void OnCreatureRemoved(CreatureId id) override;
  void Tick(IWorldPerception &perception, ICreatureActivitySink &sink,
            float dt) override;

private:
  void ResetWanderState(CreatureId id, float intervalMin, float intervalMax);

  std::unordered_set<CreatureId> members_;
  std::unordered_map<CreatureId, WanderAgentState> State;
};

} // namespace cutum

#endif
