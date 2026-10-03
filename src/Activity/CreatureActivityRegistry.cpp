#include "Activity/CreatureActivityRegistry.h"
#include "Activity/Agents/BotPlayerActivityAgent.h"
#include "Activity/Agents/FleeActivityAgent.h"
#include "Activity/Agents/MeleeAttackActivityAgent.h"
#include "Activity/Agents/WanderActivityAgent.h"
#include "Activity/CreatureActivityDirector.h"
#include <memory>

namespace cutum
{

void RegisterDefaultCreatureActivityAgents(UCreatureActivityDirector &director)
{
  director.RegisterAgent(std::make_unique<UWanderActivityAgent>());
  director.RegisterAgent(std::make_unique<UFleeActivityAgent>());
  director.RegisterAgent(std::make_unique<UMeleeAttackActivityAgent>());
  director.RegisterAgent(std::make_unique<UBotPlayerActivityAgent>());
}

} // namespace cutum
