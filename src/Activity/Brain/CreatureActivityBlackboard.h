#ifndef CREATUREACTIVITYBLACKBOARD_H
#define CREATUREACTIVITYBLACKBOARD_H

#include "Activity/CreatureActivityTypes.h"
#include "Activity/Helpers/CreatureActivityNavigation.h"
#include <glm/glm.hpp>

namespace cutum
{

enum class CreatureFsmState : uint8_t
{
  Idle,
  Flee,
  Chase,
  Attack
};

struct UCreatureActivityBlackboard
{
  CreatureFsmState state{CreatureFsmState::Idle};
  CreatureId targetId{0};
  glm::vec3 lastSeenPos{0.0f};
  float alertLevel{0.0f};
  float actionTimer{0.0f};
  CreatureNavigationState navigation;
};

} // namespace cutum

#endif
