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

  std::cout << "locomotion_state_derive_test: OK" << std::endl;
  return 0;
}
