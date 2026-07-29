#include "World/Streaming/FocusIngressPolicy.h"

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
  using cutum::EvaluateFocusIngress;
  using cutum::FocusIngressInput;
  using cutum::AllowSyncHoleFillForColumn;

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = true;
    in.pending_focus = 5;
    in.mesh_async = 0;
    in.frame_ms = 22.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(d.active, "cold hole active");
    Expect(d.promote_once, "cold hole promote");
    Expect(d.relight_floor >= 3 && d.relight_floor <= 6, "cold hole paced floor");
    Expect(!d.allow_sync_hole_fill, "cold hole blocks sync fill");
    Expect(AllowSyncHoleFillForColumn(d, true), "underfeet still allowed");
    Expect(!AllowSyncHoleFillForColumn(d, false), "non-underfeet blocked");
  }

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = true;
    in.pending_focus = 20;
    in.mesh_async = 0;
    in.frame_ms = 120.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(d.active, "cold hitch active");
    Expect(d.relight_floor == 2, "hitch floor paced enqueue");
  }

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = true;
    in.pending_focus = 5;
    in.mesh_async = 20;
    in.frame_ms = 20.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(d.active, "warm pool still active with pending hole");
    Expect(d.allow_sync_hole_fill, "warm pool allows sync fill");
    Expect(d.relight_floor == 0, "warm pool no dedicated cold floor");
  }

  {
    FocusIngressInput in;
    in.moving = false;
    in.missing_mesh = true;
    in.pending_focus = 5;
    in.mesh_async = 0;
    in.frame_ms = 16.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(!d.active, "idle not cruise ingress");
  }

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = true;
    in.pending_focus = 0;
    in.mesh_async = 0;
    in.frame_ms = 16.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(d.active, "missing without pending → SoT frontier active");
    Expect(d.first_mesh_admit >= 2, "missing → first_mesh admit boost");
  }

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = false;
    in.pending_focus = 0;
    in.unfinished_visual = 0;
    in.stale_dark_near = 0;
    in.mesh_async = 0;
    in.frame_ms = 16.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(!d.active, "no frontier signal → inactive");
  }

  {
    FocusIngressInput in;
    in.moving = true;
    in.missing_mesh = false;
    in.pending_focus = 2;
    in.unfinished_visual = 1;
    in.stale_dark_near = 20;
    in.mesh_async = 2;
    in.frame_ms = 20.0;
    const auto d = EvaluateFocusIngress(in);
    Expect(d.active, "stale-dark + unfinished → frontier active");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "focus_ingress_policy_test: OK\n";
  return EXIT_SUCCESS;
}
