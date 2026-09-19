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

  Expect(ShouldRemeshStickyOverlayPeer(true, true, false, false),
         "any active overlay remeshes outside sea-band");
  Expect(ShouldRemeshStickyOverlayPeer(true, false, true, false),
         "face-toward overlay remeshes outside sea-band");
  Expect(!ShouldRemeshStickyOverlayPeer(true, false, false, false),
         "no overlay and outside band: skip");
  Expect(ShouldRemeshStickyOverlayPeer(true, false, false, true),
         "sea-band peer without sticky still remeshes (legacy)");
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
