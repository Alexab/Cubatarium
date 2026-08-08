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
    in.frame_ms = 300.0;
    in.miss_first_budget = true;
    in.era18_vb_capture_floor = true;
    const auto d = EvaluateFrameStreamingBudget(in);
    Expect(d.soft_defer_capture_budget == 0, "hitch VB: Capture floor off");
    Expect(!d.apply_vb_bg_floor, "hitch VB: bg floor off");
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

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "frame_streaming_budget_test: OK\n";
  return EXIT_SUCCESS;
}
