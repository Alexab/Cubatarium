#ifndef CREATUREINTENT_H
#define CREATUREINTENT_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

struct CreatureIntent
{
  glm::vec3 moveDirWorld{0.0f};
  float moveSpeed{0.0f};
  bool wantJump{false};
  bool clearOnApply{true};
  LocomotionState suggestedAnim{LocomotionState::Idle};
  glm::vec3 lookAtWorld{0.0f};
  float lookAtWeight{0.0f};
};

} // namespace cutum

#endif
