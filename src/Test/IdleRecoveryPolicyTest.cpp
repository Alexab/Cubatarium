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
  using cutum::kIdleCalmStickyRemnant;
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
    // Era14.1 C: mid-wall sticky needs budget≥2 to clear IDLE_CLEAN sticky=1.
    Expect(d.budget == 2, "sticky drain mid-wall budget 2");
  }

  {
    StickyRemeshDrainInput in;
    in.black_sticky = 1;
    in.last_frame_ms = 40.0;
    const auto d = EvaluateStickyRemeshDrain(in);
    Expect(d.run_drain, "calm sticky drains");
    Expect(d.budget == 2, "calm sticky=1 budget 2");
  }

  {
    // Era14: wall>80 no longer skips FOV sticky remesh (budget=1).
    StickyRemeshDrainInput in;
    in.black_sticky = 1;
    in.last_frame_ms = 160.0;
    const auto d = EvaluateStickyRemeshDrain(in);
    Expect(d.run_drain, "hot wall still drains sticky");
    Expect(d.budget == 1, "hot wall sticky budget 1");
  }

  {
    // Era22: idle underfeet flicker — do not drain every frame.
    StickyRemeshDrainInput in;
    in.black_sticky = 2;
    in.last_frame_ms = 40.0;
    in.moving = false;
    in.frames_since_last_drain = 5;
    const auto d = EvaluateStickyRemeshDrain(in);
    Expect(!d.run_drain, "idle sticky must not drain every frame");
  }

  {
    StickyRemeshDrainInput in;
    in.black_sticky = 2;
    in.last_frame_ms = 40.0;
    in.moving = true;
    in.frames_since_last_drain = 0;
    const auto d = EvaluateStickyRemeshDrain(in);
    Expect(d.run_drain, "moving sticky drain ignores idle cadence");
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
    Expect(d.emerge_total_budget_ms <= 8.0, "calm idle hot wall emerge<=8");
    Expect(d.sync_cap == 0, "calm idle hot wall sync off");
  }

  {
    // I4c/I4d: idle-clean calm periods had black_sticky=2; pace sync only.
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.black_sticky = kIdleCalmStickyRemnant;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(d.active, "remnant sticky still caps drain");
    Expect(d.sync_cap == 0, "remnant sticky sync off");
    Expect(d.emerge_total_budget_ms <= 12.0, "remnant sticky emerge<=12");
    Expect(d.mesh_drain >= 8, "remnant sticky keeps remesh drain");
    Expect(d.mesh_drain <= 8, "remnant sticky drain capped at 8");
  }

  {
    // I4e: high sticky must still kill SyncRebuild when no light/mesh debt.
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.black_sticky = 8;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(d.active, "high sticky still caps when no pending");
    Expect(d.sync_cap == 0, "high sticky sync off");
    Expect(d.mesh_drain >= 8, "high sticky keeps remesh drain");
  }

  {
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.missing_visible_mesh = true;
    in.rim_only_visible_miss = true;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(d.active, "rim-only miss still gets calm cap");
    Expect(d.mesh_drain <= 2, "rim-only miss drain capped");
  }

  {
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.black_sticky = kIdleCalmStickyRemnant + 1;
    in.pending_focus_count = 1;
    in.last_frame_ms = 86.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(!d.active, "pending light skips calm cap");
  }

  {
    IdleMeshDrainCapInput in;
    in.moving = false;
    in.last_frame_ms = 40.0;
    in.mesh_drain = 14;
    in.mesh_schedule = 12;
    const auto d = EvaluateIdleMeshDrainCap(in);
    Expect(d.active, "calm idle mid wall caps drain");
    Expect(d.emerge_total_budget_ms <= 10.0, "calm idle mid wall emerge<=10");
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
