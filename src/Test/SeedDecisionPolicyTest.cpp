#include "World/Streaming/SeedDecisionPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using cutum::EvaluateSeedDecision;
  using cutum::SeedDecisionInput;

  {
    SeedDecisionInput in;
    in.near_focus = true;
    in.can_seed = true;
    in.moving_cruise = true;
    in.frame_ms = 18.0;
    in.visual_holes = 2;
    const auto d = EvaluateSeedDecision(in);
    Expect(d.try_sync_seed, "cruise+CanSeed+healthy → try_sync");
    Expect(d.cheap_seed, "cruise uses cheap seed");
    Expect(d.budget_ms >= 1.5 && d.budget_ms <= 2.0, "cruise budget 1.5–2ms");
  }

  {
    SeedDecisionInput in;
    in.near_focus = true;
    in.can_seed = true;
    in.moving_cruise = true;
    in.frame_ms = 45.0;
    const auto d = EvaluateSeedDecision(in);
    Expect(!d.try_sync_seed, "hot cruise → no sync");
    Expect(d.enqueue_pending, "hot cruise → FIFO");
  }

  {
    SeedDecisionInput in;
    in.underfeet = true;
    in.near_focus = true;
    in.can_seed = true;
    in.moving_cruise = false;
    in.frame_ms = 14.0;
    in.visual_holes = 0;
    const auto d = EvaluateSeedDecision(in);
    Expect(d.try_sync_seed, "idle underfeet try_sync");
    Expect(!d.cheap_seed, "idle underfeet full Relight budget");
    Expect(d.budget_ms == 3.0, "idle underfeet budget 3ms");
  }

  {
    SeedDecisionInput in;
    in.near_focus = true;
    in.can_seed = false;
    in.moving_cruise = false;
    in.frame_ms = 20.0;
    const auto d = EvaluateSeedDecision(in);
    Expect(!d.try_sync_seed, "no CanSeed → no sync");
    Expect(d.enqueue_pending, "no CanSeed → PendingLight enqueue");
  }

  {
    SeedDecisionInput in;
    in.near_focus = true;
    in.can_seed = true;
    in.moving_cruise = false;
    in.underfeet = false;
    in.frame_ms = 18.0;
    const auto d = EvaluateSeedDecision(in);
    Expect(d.try_sync_seed, "idle near_focus healthy frame try_sync");
    Expect(d.budget_ms == 2.0, "idle near_focus budget 2ms");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "seed_decision_policy_test: OK\n";
  return EXIT_SUCCESS;
}
