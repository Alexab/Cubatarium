#include "activity/CreatureActivityRegistry.h"
#include "activity/CreatureActivityDirector.h"
#include "activity/agents/WanderActivityAgent.h"
#include <memory>

namespace cutum {

void RegisterDefaultCreatureActivityAgents(CreatureActivityDirector& director)
{
 director.RegisterAgent(std::make_unique<WanderActivityAgent>());
}

} // namespace cutum
