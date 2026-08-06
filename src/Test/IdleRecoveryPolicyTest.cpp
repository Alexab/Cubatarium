#include "World/Streaming/IdleRecoveryPolicy.h"

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
  using cutum::ComputeIdleRecoveryBgBudget;
  using cutum::EvaluateIdleVisualDrain;
  using cutum::EvaluateStickyRemeshDrain;
  using cutum::IdleRecoveryBgBudgetInput;
  using cutum::IdleVisualDrainInput;
  using cutum::StickyRemeshDrainInput;

  {
    IdleVisualDrainInput in;
    in.last_frame_ms = 20.0;
    in.idle_visual_drain_cd = 0;
    in.pending_focus_count = 5;
    const auto d = EvaluateIdleVisualDrain(in);
    Expect(d.run_drain, "fast frame runs drain");
    Expect(d.budget >= 4, "fast frame full budget");
  }

  {
    IdleVisualDrainInput in;
    in.last_frame_ms = 125.0;
    in.idle_visual_drain_cd = 0;
    in.pending_focus_count = 0;
    const auto d = EvaluateIdleVisualDrain(in);
    Expect(!d.run_drain, "very slow frame no pending skips drain");
  }

  {
    IdleVisualDrainInput in;
    in.last_frame_ms = 125.0;
    in.idle_visual_drain_cd = 0;
    in.pending_focus_count = 35;
    const auto d = EvaluateIdleVisualDrain(in);
    Expect(!d.run_drain, "very slow frame skips drain even with pending");
  }

  {
    IdleVisualDrainInput in;
    in.last_frame_ms = 100.0;
    in.idle_visual_drain_cd = 0;
    in.pending_focus_count = 35;
    const auto d = EvaluateIdleVisualDrain(in);
    Expect(d.run_drain, "slow frame heavy pending runs drain");
    Expect(d.budget == 1, "slow heavy pending budget 1");
  }

  {
    IdleVisualDrainInput in;
    in.last_frame_ms = 45.0;
    in.idle_visual_drain_cd = 0;
    in.pending_focus_count = 10;
    const auto d = EvaluateIdleVisualDrain(in);
    Expect(d.run_drain, "mid frame runs drain");
    Expect(d.budget <= 3, "mid frame reduced budget");
    Expect(!d.allow_sync, "mid frame no sync");
  }

  {
    IdleRecoveryBgBudgetInput in;
    in.idle_recovery = true;
    in.frame_ms = 100.0;
    in.k_bad_frame_ms = 24.0;
    in.pending_light_focus_n = 20;
    in.bg_budget_in = 1;
    const int bg = ComputeIdleRecoveryBgBudget(in);
    Expect(bg <= 4, "hot idle recovery capped at 4");
  }

  {
    IdleRecoveryBgBudgetInput in;
    in.idle_recovery = true;
    in.frame_ms = 20.0;
    in.k_bad_frame_ms = 24.0;
    in.pending_light_focus_n = 20;
    in.bg_budget_in = 1;
    const int bg = ComputeIdleRecoveryBgBudget(in);
    Expect(bg >= 32, "cold idle recovery keeps high budget");
  }

  {
    IdleRecoveryBgBudgetInput in;
    in.idle_recovery = false;
    in.frame_ms = 100.0;
    in.bg_budget_in = 2;
    const int bg = ComputeIdleRecoveryBgBudget(in);
    Expect(bg == 2, "non-idle recovery unchanged");
  }

  {
    StickyRemeshDrainInput in;
    in.black_sticky = 3;
    in.last_frame_ms = 60.0;
    const auto d = EvaluateStickyRemeshDrain(in);
    Expect(d.run_drain, "sticky drain extended to 80ms");
    Expect(d.budget == 1, "sticky drain mid-wall budget 1");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "idle_recovery_policy_test: OK\n";
  return EXIT_SUCCESS;
}
