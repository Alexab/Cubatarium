#include "World/Streaming/SeaSeamRemeshPolicy.h"

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
  using cutum::ShouldRemeshStickyOverlayPeer;

  // After 185830 regress: any-active outside band must NOT remesh (blacks).
  Expect(!ShouldRemeshStickyOverlayPeer(true, true, false, false),
         "any-active outside sea-band: no remesh (regress KEEP)");
  Expect(ShouldRemeshStickyOverlayPeer(true, false, true, false),
         "face-toward remeshes outside sea-band");
  Expect(!ShouldRemeshStickyOverlayPeer(true, false, false, false),
         "no overlay and outside band: skip");
  Expect(ShouldRemeshStickyOverlayPeer(true, true, false, true),
         "active overlay in sea-band: remesh");
  Expect(!ShouldRemeshStickyOverlayPeer(false, true, true, true),
         "non-drawable peer: skip");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "StickyOverlayRemeshPolicyTest: PASS\n";
  return 0;
}
