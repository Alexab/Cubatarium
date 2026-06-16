#include "Activity/CreatureActivityRegistry.h"
#include "Activity/Agents/WanderActivityAgent.h"
#include "Activity/CreatureActivityDirector.h"
#include <memory>

namespace cutum
{

void RegisterDefaultCreatureActivityAgents(UCreatureActivityDirector &director)
{
  director.RegisterAgent(std::make_unique<UWanderActivityAgent>());
}

} // namespace cutum
