#include "Creatures/Locomotion/LocomotionStateDerive.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "locomotion_state_derive_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::CreatureLocomotionCapabilities;
  using cutum::CreatureLocomotionFacts;
  using cutum::CreatureLocomotionRawInput;
  using cutum::DeriveLocomotionState;
  using cutum::FinalizeLocomotionFacts;
  using cutum::LocomotionArchetype;
  using cutum::LocomotionState;

  CreatureLocomotionCapabilities caps;
  caps.walkSpeed = 4.0f;

  CreatureLocomotionFacts raw;
  raw.onGround = true;
  raw.horizontalSpeed = 0.0f;

  CreatureLocomotionRawInput hint;
  hint.hasSuggestedAnim = true;
  hint.suggestedAnim = LocomotionState::Run;
  hint.dt = 1.0f / 60.0f;

  Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialBiped, raw, caps,
                               &hint) == LocomotionState::Idle,
         "zero speed must not force Run from suggestedAnim");

  raw.horizontalSpeed = 2.0f;
  Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialBiped, raw, caps,
                               &hint) == LocomotionState::Walk,
         "nonzero slow speed should Walk from speed not hint");

  raw.horizontalSpeed = 0.0f;
  raw.animPhase = 1.0f;
  FinalizeLocomotionFacts(raw, caps, hint, 1.5f, hint.dt);
  Expect(raw.state == LocomotionState::Idle,
         "finalize at zero speed should Idle despite Run hint");

  // Walk intent: a one-frame displacement spike (2.5x walk speed: step down,
  // neighbour push) must not flip Walk -> Run.
  {
    CreatureLocomotionFacts spike;
    spike.onGround = true;
    spike.horizontalSpeed = caps.walkSpeed * 2.5f;
    CreatureLocomotionRawInput walkHint;
    walkHint.hasSuggestedAnim = true;
    walkHint.suggestedAnim = LocomotionState::Walk;
    walkHint.previousState = LocomotionState::Walk;
    Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialQuadruped,
                                 spike, caps, &walkHint) == LocomotionState::Walk,
           "walk intent stays Walk through a speed spike");
    // No intent hint: speed alone still decides (knockback, slides).
    Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialQuadruped,
                                 spike, caps, nullptr) == LocomotionState::Run,
           "without a hint a fast body still reads as Run");
  }
  // Run intent: once running, a collision slow-down keeps Run (hysteresis);
  // entering Run still needs speed above walk.
  {
    CreatureLocomotionFacts slow;
    slow.onGround = true;
    slow.horizontalSpeed = caps.walkSpeed * 0.5f;
    CreatureLocomotionRawInput runHint;
    runHint.hasSuggestedAnim = true;
    runHint.suggestedAnim = LocomotionState::Run;
    runHint.previousState = LocomotionState::Run;
    Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialBiped, slow,
                                 caps, &runHint) == LocomotionState::Run,
           "running agent slowed by a wall keeps Run");
    runHint.previousState = LocomotionState::Walk;
    Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialBiped, slow,
                                 caps, &runHint) == LocomotionState::Walk,
           "slow agent does not enter Run from the hint alone");
    slow.horizontalSpeed = 0.0f;
    runHint.previousState = LocomotionState::Run;
    Expect(DeriveLocomotionState(LocomotionArchetype::TerrestrialBiped, slow,
                                 caps, &runHint) == LocomotionState::Idle,
           "a stopped runner idles");
  }

  // Frozen creature (chunk unloaded): stands still instead of walking in place.
  CreatureLocomotionFacts frozen;
  frozen.state = LocomotionState::Walk;
  frozen.horizontalSpeed = 2.1f;
  frozen.verticalSpeed = -0.5f;
  frozen.animPhase = 3.0f;
  SettleFrozenLocomotionFacts(frozen);
  Expect(frozen.state == LocomotionState::Idle && frozen.horizontalSpeed == 0.0f &&
             frozen.verticalSpeed == 0.0f,
         "frozen walker idles at zero speed");
  Expect(frozen.animPhase == 3.0f, "freeze keeps the anim phase");
  frozen.state = LocomotionState::Fly;
  frozen.horizontalSpeed = 2.85f;
  SettleFrozenLocomotionFacts(frozen);
  Expect(frozen.state == LocomotionState::Hover && frozen.horizontalSpeed == 0.0f,
         "frozen flyer hovers in place");
  frozen.state = LocomotionState::Swim;
  SettleFrozenLocomotionFacts(frozen);
  Expect(frozen.state == LocomotionState::Idle, "frozen swimmer idles");

  std::cout << "locomotion_state_derive_test: OK" << std::endl;
  return 0;
}
