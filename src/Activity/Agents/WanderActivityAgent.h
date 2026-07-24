#ifndef WANDERACTIVITYAGENT_H
#define WANDERACTIVITYAGENT_H

#include "Activity/CreatureActivityTypes.h"
#include "Activity/IUCreatureActivityAgent.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

void NormalizeWanderIntervalRange(float rawMin, float rawMax, float &outMin,
                                  float &outMax);

struct WanderAgentState
{
  float timer{0.0f};
  glm::vec3 direction{1.0f, 0.0f, 0.0f};
  glm::vec3 lastBodyOrigin{};
  float stuckTimer{0.0f};
  float idleTimer{0.0f};
  float intervalMin{2.0f};
  float intervalMax{4.0f};
  bool forceRepick{false};
};

class UWanderActivityAgent : public IUCreatureActivityAgent
{
public:
  const char *GetBehaviorId() const override { return "wander"; }
  void OnCreatureAdded(CreatureId Id,
                       const CreatureBehaviorParams &behavior) override;
  void OnCreatureRemoved(CreatureId Id) override;
  void Tick(IUWorldPerception &perception, IUCreatureActivitySink &sink,
            float dt) override;

private:
  void ResetWanderState(CreatureId Id, float intervalMin, float intervalMax);

  std::unordered_set<CreatureId> Members;
  std::unordered_map<CreatureId, WanderAgentState> State;
};

} // namespace cutum

#endif
