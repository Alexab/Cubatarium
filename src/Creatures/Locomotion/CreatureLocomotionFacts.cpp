#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Locomotion/LocomotionStateDerive.h"
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

float AdvanceAnimPhase(float phase, float horizontalSpeed, float walkCycleHz,
                       float walkSpeedRef, float dt)
{
  if (horizontalSpeed < 0.05f || walkCycleHz <= 0.0f || walkSpeedRef <= 1e-4f ||
      dt <= 0.0f)
  {
    return phase;
  }
  constexpr float kTwoPi = 6.283185307f;
  const float speedRatio = horizontalSpeed / walkSpeedRef;
  return phase + kTwoPi * walkCycleHz * speedRatio * dt;
}

void SettleFrozenLocomotionFacts(CreatureLocomotionFacts &facts)
{
  const bool airborne = facts.state == LocomotionState::Fly ||
                        facts.state == LocomotionState::Hover ||
                        facts.state == LocomotionState::Glide;
  facts.state = airborne ? LocomotionState::Hover : LocomotionState::Idle;
  facts.horizontalSpeed = 0.0f;
  facts.verticalSpeed = 0.0f;
}

int ConsumeFootstepAudio(const CreatureLocomotionFacts &facts,
                         CreatureFootstepAudioState &state)
{
  constexpr float kTwoPi = 6.283185307f;
  constexpr float kHalfCycle = kTwoPi * 0.5f;
  constexpr float kMoveEpsilon = 0.05f;

  const bool moving = facts.horizontalSpeed >= kMoveEpsilon;
  int crossing = 0;

  if (state.Seeded && facts.onGround && !state.PrevOnGround)
  {
    crossing = -1; // grounded transition: landing
  }

  if (moving && facts.onGround && !facts.swimming && facts.mode!=CreatureMovementMode::Flying)
  {
    // Accumulate only actual forward phase motion (idle drift is skipped);
    // each half-cycle boundary is one footfall of the walk cycle.
    const float delta = facts.animPhase - state.PrevPhase;
    if (delta > 0.f && delta < kTwoPi)
    {
      const float before = state.AccumPhase;
      state.AccumPhase += delta;
      if (!state.Seeded)
      {
        crossing = 0;
      }
      else if (static_cast<int>(before / kHalfCycle) !=
               static_cast<int>(state.AccumPhase / kHalfCycle))
      {
        crossing = 1;
      }
    }
  }

  state.PrevPhase = facts.animPhase;
  state.PrevOnGround = facts.onGround;
  state.Seeded = true;
  return crossing;
}

void FillTerrestrialRawFacts(CreatureLocomotionFacts &out,
                             const CreatureLocomotionRawInput &input,
                             LocomotionArchetype archetype, float bodyYaw,
                             float bodyPitch)
{
  out.archetype = archetype;
  out.bodyYaw = bodyYaw;
  out.bodyPitch = bodyPitch;
  if (!input.locomotion)
  {
    return;
  }
  const UCreatureLocomotionController &loc = *input.locomotion;
  out.mode = loc.GetMode();
  out.onGround = loc.IsOnGround();
  out.stanceBlend = loc.GetStanceBlend();
  out.verticalSpeed = loc.GetVerticalVelocity();
  if (input.dt > 1e-6f)
  {
    const glm::vec3 delta = input.bodyOriginAfter - input.bodyOriginBefore;
    // XZ only, aerials included: vertical bob is not travel. A hovering
    // wasp with a blocked horizontal wish derived Fly from the 3D speed and
    // "stood in Fly" forever (its anim never returned to Hover).
    out.horizontalSpeed = glm::length(glm::vec2(delta.x, delta.z)) / input.dt;
  }
  else
  {
    out.horizontalSpeed = 0.0f;
  }
}

void FinalizeLocomotionFacts(CreatureLocomotionFacts &facts,
                             const CreatureLocomotionCapabilities &caps,
                             const CreatureLocomotionRawInput &input,
                             float walkCycleHz, float dt)
{
  facts.state = DeriveLocomotionState(facts.archetype, facts, caps, &input);
  constexpr float kTwoPi = 6.283185307f;
  // Phase follows actual travel only — do not fake walk while scraped/stuck.
  if (facts.horizontalSpeed < 0.05f && dt > 0.0f)
  {
    // Idle/blocked: slow phase drift only — do not fake walk from agent hints.
    facts.animPhase += kTwoPi * 0.35f * dt;
  }
  else
  {
    facts.animPhase = AdvanceAnimPhase(facts.animPhase, facts.horizontalSpeed,
                                       walkCycleHz, caps.walkSpeed, dt);
  }
}

} // namespace cutum
