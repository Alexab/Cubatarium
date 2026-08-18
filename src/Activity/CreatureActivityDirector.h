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
  static constexpr float kMaxCognitiveDt = 0.1f;
  static constexpr int kStressAgentsPerTickCap = 8;

  void RegisterAgent(std::unique_ptr<IUCreatureActivityAgent> agent);
  void Clear();
  void OnCreatureAdded(CreatureId Id, const std::string &behaviorId,
                       const CreatureBehaviorParams *behavior = nullptr);
  void OnCreatureRemoved(CreatureId Id);
  void TickAgents(IUWorldPerception &perception, IUCreatureActivitySink &sink,
                  float dt, bool stress_tick = false);
  void SetActivityTickInterval(float seconds);
  float GetActivityTickInterval() const { return ActivityTickInterval; }
  int GetLastAgentsTicked() const { return LastAgentsTicked; }
  int GetLastAgentsDeferred() const { return LastAgentsDeferred; }

private:
  IUCreatureActivityAgent *FindAgent(const std::string &behaviorId) const;

  std::vector<std::unique_ptr<IUCreatureActivityAgent>> Agents;
  std::unordered_map<std::string, IUCreatureActivityAgent *> AgentsByBehaviorId;
  std::unordered_map<CreatureId, IUCreatureActivityAgent *> Membership;
  float ActivityTickInterval{0.05f};
  float AccumulatedTickDt{0.0f};
  size_t AgentRoundRobinCursor{0};
  int LastAgentsTicked{0};
  int LastAgentsDeferred{0};
};

} // namespace cutum

#endif
