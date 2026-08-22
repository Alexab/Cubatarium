#include "World/Streaming/FrameStreamingBudget.h"

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
  using cutum::EvaluateFrameStreamingBudget;
  using cutum::EvaluateMissFirstDrainN;
  using cutum::FrameStreamingBudgetInput;

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = true;
    in.moving = false;
    in.miss_first_budget = false;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.capture_first_mesh_only, "miss ⇒ FirstMesh-only capture kind");
    Expect(d.soft_defer_capture_budget == 2, "legacy miss idle floor 2");
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = true;
    in.visible_black_n = 20;
    in.frame_ms = 280.0;
    in.bad_frame_ms = 24.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.capture_first_mesh_only, "miss-first: FirstMesh only");
    Expect(d.soft_defer_capture_budget == 1, "miss-first: Capture ≤1");
    Expect(d.heal_deferred_for_miss, "miss-first hitch/VB defers heal");
    Expect(!d.apply_vb_bg_floor || d.vb_bg_budget_floor <= 1,
           "miss-first hitch: no VB bg storm");
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = false;
    in.visible_black_n = 10;
    in.frame_ms = 45.0; // typical idle wall — not hot
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget == 2, "mid idle VB: keep Capture floor");
    // ColdWall S2a: VB alone without PL → no bg Capture floor.
    Expect(!d.apply_vb_bg_floor, "ColdWall S2a: VB without PL → no bg floor");
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = false;
    in.visible_black_n = 10;
    in.pending_light_focus_n = 4;
    in.frame_ms = 45.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.apply_vb_bg_floor, "ColdWall S2a: VB+PL → keep bg floor");
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = false;
    in.visible_black_n = 10;
    in.frame_ms = 300.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget == 1, "Era20: hot VB !miss keeps Relight=1");
    Expect(!d.apply_vb_bg_floor,
           "ColdWall S2a: hot VB without PL → no bg floor");
    // Frontier pressure (VB heal + empty gen) forces FirstMesh Capture kind.
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = false;
    in.visible_black_n = 10;
    in.pending_light_focus_n = 2;
    in.frame_ms = 300.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.apply_vb_bg_floor && d.vb_bg_budget_floor == 1,
           "ColdWall S2a: hot VB+PL keeps bg floor 1");
  }

  {
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = true;
    in.visible_black_n = 10;
    in.frame_ms = 300.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget == 1, "Era21: miss hot keeps Capture FM=1");
    Expect(d.capture_first_mesh_only, "Era21: miss hot Capture is FirstMesh");
    Expect(d.apply_vb_bg_floor && d.vb_bg_budget_floor == 1,
           "Era21 I-V2: miss+hot VB mid-floor 1");
    Expect(d.heal_deferred_for_miss, "Era21: heal_deferred telem under miss+VB");
  }

  {
    FrameStreamingBudgetInput in;
    in.pending_light_focus_n = 12;
    in.frame_ms = 40.0;
    in.hot_frame_ms = 80.0;
    in.miss_first_budget = true;
    in.era18_vb_bg_budget_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget >= 1, "calm pending: Capture mid floor");
    Expect(!d.capture_first_mesh_only, "calm pending: Relight Capture kind");
  }

  {
    FrameStreamingBudgetInput in;
    in.visible_black_n = 5;
    in.miss_first_budget = false;
    in.era18_vb_capture_floor = false;
    in.era18_vb_bg_budget_floor = false;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget == 0, "kill-switch off: no VB Capture");
    Expect(!d.apply_vb_bg_floor, "kill-switch off: no VB bg floor");
  }

  {
    const int d = EvaluateMissFirstDrainN(
        /*recover_n=*/2, /*miss=*/true, /*no_ticket=*/false, /*vb=*/true,
        /*hitch=*/true, /*moving=*/false, /*miss_first=*/true);
    Expect(d >= 10, "miss-first: FirstMesh drain bump under miss");
  }

  {
    const int d = EvaluateMissFirstDrainN(
        /*recover_n=*/2, /*miss=*/false, /*no_ticket=*/false, /*vb=*/true,
        /*hitch=*/true, /*moving=*/false, /*miss_first=*/true);
    Expect(d == 2, "miss-first: hitch VB alone does not storm drain");
  }

  {
    // Era25 I-F4: frontier_pressure dual-queue under miss+gen+void.
    FrameStreamingBudgetInput in;
    in.missing_visible_mesh = true;
    in.miss_first_budget = true;
    in.era18_vb_bg_budget_floor = true;
    in.gen_backlog = 8;
    in.async_queued = 0;
    in.void_n = 412;
    in.frame_ms = 90.0;
    in.hot_frame_ms = 80.0;
    in.visible_black_n = 0;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget >= 1, "Era25: frontier Capture FM≥1");
    Expect(d.capture_first_mesh_only, "Era25: frontier Capture FirstMesh-only");
    Expect(d.apply_vb_bg_floor && d.vb_bg_budget_floor >= 1,
           "Era25: frontier void Relight bg floor≥1 (no starve under miss)");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "frame_streaming_budget_test: OK\n";
  return EXIT_SUCCESS;
}
