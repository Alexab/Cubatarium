#include "CreatureLocomotionFacts.h"
#include "CreatureLocomotionController.h"
#include "LocomotionStateDerive.h"
#include <cmath>
#include <glm/glm.hpp>

namespace cutum {

float AdvanceAnimPhase(float phase, float horizontalSpeed, float walkCycleHz, float walkSpeedRef,
                     float dt)
{
 if (horizontalSpeed < 0.05f || walkCycleHz <= 0.0f || walkSpeedRef <= 1e-4f || dt <= 0.0f) {
  return phase;
 }
 constexpr float kTwoPi = 6.283185307f;
 const float speedRatio = horizontalSpeed / walkSpeedRef;
 return phase + kTwoPi * walkCycleHz * speedRatio * dt;
}

void FillTerrestrialRawFacts(CreatureLocomotionFacts& out, const CreatureLocomotionRawInput& input,
                             LocomotionArchetype archetype, float bodyYaw, float bodyPitch)
{
 out.archetype = archetype;
 out.bodyYaw = bodyYaw;
 out.bodyPitch = bodyPitch;
 if (!input.locomotion) {
  return;
 }
 const CreatureLocomotionController& loc = *input.locomotion;
 out.mode = loc.GetMode();
 out.onGround = loc.IsOnGround();
 out.stanceBlend = loc.GetStanceBlend();
 out.verticalSpeed = loc.GetVerticalVelocity();
 if (input.dt > 1e-6f) {
  const glm::vec3 delta = input.bodyOriginAfter - input.bodyOriginBefore;
  out.horizontalSpeed = glm::length(glm::vec2(delta.x, delta.z)) / input.dt;
 } else {
  out.horizontalSpeed = 0.0f;
 }
}

void FinalizeLocomotionFacts(CreatureLocomotionFacts& facts,
                             const CreatureLocomotionCapabilities& caps,
                             const CreatureLocomotionRawInput& input, float walkCycleHz,
                             float dt)
{
 facts.state = DeriveLocomotionState(facts.archetype, facts, caps, &input);
 facts.animPhase =
     AdvanceAnimPhase(facts.animPhase, facts.horizontalSpeed, walkCycleHz, caps.walkSpeed, dt);
}

} // namespace cutum
