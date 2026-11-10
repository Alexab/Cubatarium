#include <cstdlib>
#include <iostream>

// Minimal pure policy checks for visual-ready window math without linking World.

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

bool VisualReadyForRing(bool lit_ready, bool render_ready, int cheb,
                        int focus_radius, double since_stop)
{
  if (!lit_ready)
  {
    return false;
  }
  const bool in_strict = cheb <= focus_radius;
  const bool strict_window = since_stop > 0.0 && since_stop <= 8.0;
  if (in_strict && strict_window)
  {
    return render_ready;
  }
  return true;
}

} // namespace

int main()
{
  Expect(VisualReadyForRing(true, false, 0, 4, 0.0),
         "moving (since_stop=0) keep → LitReady enough");
  Expect(!VisualReadyForRing(false, true, 0, 4, 0.0), "not lit → not ready");
  Expect(!VisualReadyForRing(true, false, 1, 4, 2.0),
         "strict stop window requires RenderReady");
  Expect(VisualReadyForRing(true, true, 1, 4, 2.0),
         "strict stop + RenderReady → ready");
  Expect(VisualReadyForRing(true, false, 8, 4, 2.0),
         "outside focus cheb keep → LitReady enough");
  Expect(VisualReadyForRing(true, false, 0, 4, 9.0),
         "after 8s stop window → keep LitReady");

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return EXIT_FAILURE;
  }
  std::cout << "visual_ready_for_ring_policy_test: OK\n";
  return EXIT_SUCCESS;
}
