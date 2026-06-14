#include "activity/CreatureActivityRegistry.h"
#include "activity/CreatureActivityDirector.h"
#include "activity/agents/WanderActivityAgent.h"
#include <memory>

namespace cutum
{

void RegisterDefaultCreatureActivityAgents(UCreatureActivityDirector &director)
{
  director.RegisterAgent(std::make_unique<UWanderActivityAgent>());
}

} // namespace cutum
