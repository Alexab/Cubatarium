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
  using cutum::EvaluateIdleFocusDirtyDebt;
  using cutum::EvaluateIdleMeshDrainCap;
  using cutum::EvaluateIdleVisualDrain;
  using cutum::EvaluateStickyRemeshDrain;
  using cutum::IdleFocusDirtyDebtInput;
  using cutum::IdleMeshDrainCapInput;
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

  {
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(d.active, "calm idle hot wall caps drain");
    Expect(d.mesh_drain <= 2, "calm idle hot wall drain<=2");
    Expect(d.mesh_schedule <= 3, "calm idle hot wall schedule<=3");
  }

  {
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.pending_focus_count = 4;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(!d.active, "pending debt skips calm cap");
    Expect(d.mesh_drain == 14, "pending debt unchanged");
  }

  {
    IdleFocusDirtyDebtInput in;
    in.moving = false;
    in.pending_focus_count = 0;
    in.black_sticky = 0;
    in.missing_visible_mesh = false;
    in.focus_dirty_early = 300;
    in.prev_focus_dirty = 300;
    in.high_frames = 2;
    const auto d = EvaluateIdleFocusDirtyDebt(in);
    Expect(d.active, "dirty debt latch activates after persistence");
  }

  {
    IdleFocusDirtyDebtInput in;
    in.moving = false;
    in.pending_focus_count = 0;
    in.black_sticky = 0;
    in.missing_visible_mesh = false;
    in.focus_dirty_early = 260;
    in.prev_focus_dirty = 300;
    in.high_frames = 5;
    const auto d = EvaluateIdleFocusDirtyDebt(in);
    Expect(!d.active, "dirty debt clears below threshold");
    Expect(d.high_frames_next == 0, "dirty debt frame counter resets");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "idle_recovery_policy_test: OK\n";
  return EXIT_SUCCESS;
}
