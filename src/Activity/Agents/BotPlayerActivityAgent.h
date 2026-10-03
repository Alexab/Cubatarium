#ifndef BOTPLAYERACTIVITYAGENT_H
#define BOTPLAYERACTIVITYAGENT_H

#include "Activity/Brain/CreatureActivityBlackboard.h"
#include "Activity/IUCreatureActivityAgent.h"
#include <unordered_map>
#include <unordered_set>

namespace cutum
{

/// Player-like bot: follow controlled creature, melee nearby threats, wander.
class UBotPlayerActivityAgent : public IUCreatureActivityAgent
{
public:
  const char *GetBehaviorId() const override { return "bot_player"; }
  void OnCreatureAdded(CreatureId Id,
                       const CreatureBehaviorParams &behavior) override;
  void OnCreatureRemoved(CreatureId Id) override;
  void Tick(IUWorldPerception &perception, IUCreatureActivitySink &sink,
            float dt) override;

private:
  std::unordered_set<CreatureId> Members;
  std::unordered_map<CreatureId, UCreatureActivityBlackboard> State;
  std::unordered_map<CreatureId, CreatureBehaviorParams> Behaviors;
};

} // namespace cutum

#endif
