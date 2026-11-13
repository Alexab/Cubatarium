#include "Creatures/Locomotion/CreatureLocomotionFacts.h"

#include <cmath>
#include <iostream>

namespace
{

constexpr const char *kTestName = "footstep_phase_wrap_test";

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << kTestName << " FAIL: " << msg << "\n";
    std::exit(1);
  }
}

void Advance(cutum::CreatureLocomotionFacts &facts, float speed, float dt)
{
  facts.horizontalSpeed = speed;
  facts.onGround = true;
  facts.animPhase = cutum::AdvanceAnimPhase(facts.animPhase, speed, 2.f, 4.f,
                                            dt);
}

} // namespace

int main()
{
  cutum::CreatureLocomotionFacts facts;
  cutum::CreatureFootstepAudioState state;

  // Two footfalls per full walk cycle (half-cycle crossings).
  int steps = 0;
  int landings = 0;
  const float kDt = 1.f / 60.f;
  for (int i = 0; i < 120; ++i)
  {
    Advance(facts, 4.f, kDt); // walk speed 4 m/s, cycle 2 Hz
    const int crossing = cutum::ConsumeFootstepAudio(facts, state);
    if (crossing > 0)
    {
      ++steps;
    }
    if (crossing < 0)
    {
      ++landings;
    }
  }
  // 2 s of walking at 2 Hz = 4 cycles = 8 footfalls.
  Expect(steps == 8, "two footfalls per cycle");
  Expect(landings == 0, "no landings while grounded");

  // Idle: no crossings, no crash.
  steps = 0;
  for (int i = 0; i < 60; ++i)
  {
    facts.horizontalSpeed = 0.f;
    facts.animPhase += 6.283185307f * 0.35f * kDt;
    const int crossing = cutum::ConsumeFootstepAudio(facts, state);
    if (crossing > 0)
    {
      ++steps;
    }
  }
  Expect(steps == 0, "idle drift makes no steps");

  // Airborne: no footfalls.
  steps = 0;
  for (int i = 0; i < 60; ++i)
  {
    facts.horizontalSpeed = 4.f;
    facts.onGround = false;
    facts.animPhase += 6.283185307f * kDt;
    const int crossing = cutum::ConsumeFootstepAudio(facts, state);
    if (crossing != 0)
    {
      ++steps;
    }
  }
  Expect(steps == 0, "airborne phase makes no steps");

  // Landing fires exactly once on the grounded transition.
  facts.onGround = true;
  const int landing = cutum::ConsumeFootstepAudio(facts, state);
  Expect(landing == -1, "landing on grounded transition");
  Expect(cutum::ConsumeFootstepAudio(facts, state) == 0,
         "no second landing");

  // Crouch-speed steps still fire (slow cadence).
  steps = 0;
  for (int i = 0; i < 120; ++i)
  {
    Advance(facts, 0.6f, kDt);
    const int crossing = cutum::ConsumeFootstepAudio(facts, state);
    if (crossing > 0)
    {
      ++steps;
    }
  }
  Expect(steps > 0 && steps < 8, "slow walking still steps");

  // Long-session float safety: phase keeps growing; accumulates fine.
  for (int i = 0; i < 6000; ++i)
  {
    Advance(facts, 4.f, kDt);
    cutum::ConsumeFootstepAudio(facts, state);
  }
  Expect(std::isfinite(state.AccumPhase), "accumulator stays finite");

  std::cout << kTestName << " OK\n";
  return 0;
}
