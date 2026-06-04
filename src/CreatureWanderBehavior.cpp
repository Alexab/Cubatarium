#include "CreatureWanderBehavior.h"
#include "Creature.h"
#include "CreatureIntent.h"
#include <cstdlib>
#include <glm/gtc/constants.hpp>

namespace cutum {

void ApplyWanderIntent(Creature& self, const CreatureBehaviorParams& params,
                       const CreatureLocomotionCapabilities& locomotion, float dt)
{
 if (self.IsPossessed()) {
  return;
 }

 self.TickWanderTimer(dt, params.wanderIntervalMin, params.wanderIntervalMax);
 if (self.GetWanderTimer() <= 0.0f) {
  self.ResetWanderTimer(params.wanderIntervalMin, params.wanderIntervalMax);
  const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
  self.SetWanderDirection(glm::normalize(glm::vec3(std::cos(angle), 0.0f, std::sin(angle))));
 }

 CreatureIntent intent;
 intent.moveDirWorld = self.GetWanderDirection();
 intent.moveSpeed = locomotion.walkSpeed > 0.0f ? locomotion.walkSpeed : params.moveSpeed;
 intent.suggestedAnim = LocomotionState::Walk;
 intent.clearOnApply = false;
 self.SetIntent(intent);
}

} // namespace cutum
