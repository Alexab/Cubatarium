#include "MobController.h"
#include "Creature.h"
#include "World.h"
#include <cmath>
#include <cstdlib>

namespace cutum {

void MobController::Tick(World& /*world*/, Creature& self, float dt)
{
 wanderTimer_ -= dt;
 if (wanderTimer_ <= 0.0f) {
  wanderTimer_ = 2.0f + static_cast<float>(std::rand() % 200) / 100.0f;
  const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
  wanderDir_ = glm::normalize(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
 }

 CreatureIntent intent;
 intent.moveDirWorld = wanderDir_;
 intent.moveSpeed = 2.0f;
 intent.suggestedAnim = LocomotionState::Walk;
 intent.clearOnApply = false;
 self.SetIntent(intent);
}

} // namespace cutum
