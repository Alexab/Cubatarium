#include "World/Physics/PhysicsTelemetry.h"
#include "World/Streaming/StreamerAmortizePolicy.h"

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
  using namespace cutum;

  Expect(UnloadModeRespectsExhausted(kUnloadAmortizeU0) == false, "U0 no Exhausted");
  Expect(UnloadModeRespectsExhausted(kUnloadAmortizeUA), "U-A Exhausted");
  Expect(UnloadModeUsesScanCursor(kUnloadAmortizeUA) == false, "U-A no cursor");
  Expect(UnloadModeUsesScanCursor(kUnloadAmortizeUB), "U-B cursor");
  Expect(UnloadModeDefersSave(kUnloadAmortizeUC) == false, "U-C no defer");
  Expect(UnloadModeDefersSave(kUnloadAmortizeUD), "U-D defer");

  Expect(!ShouldSkipUnloadOnStopFrame(true, 20.0, true, 100),
         "moving never skip unload via U-C gate");
  Expect(ShouldSkipUnloadOnStopFrame(false, 13.0, false, 10),
         "stop frame_ms>12 skip");
  Expect(ShouldSkipUnloadOnStopFrame(false, 5.0, true, 10),
         "stop Exhausted skip");
  Expect(ShouldSkipUnloadOnStopFrame(false, 5.0, false, 49),
         "stop dirty>48 skip");
  Expect(!ShouldSkipUnloadOnStopFrame(false, 5.0, false, 10),
         "calm stop allow unload");

  Expect(!ShouldSkipKeepShell(true, 20.0, true, kKeepShellAmortizeK0),
         "K0 ignores Exhausted/hot/uw at caller");
  Expect(ShouldSkipKeepShell(true, 5.0, false, kKeepShellAmortizeKA),
         "K-A Exhausted skip");
  Expect(ShouldSkipKeepShell(false, 15.0, false, kKeepShellAmortizeKA),
         "K-A hot frame skip");
  Expect(!ShouldSkipKeepShell(false, 5.0, true, kKeepShellAmortizeKC),
         "K-C underwater still allow");
  Expect(ShouldSkipKeepShell(false, 5.0, true, kKeepShellAmortizeKD),
         "K-D idle underwater skip");

  Expect(KeepModeUsesCheapFilter(kKeepShellAmortizeKB), "K-B cheap");
  Expect(KeepModeUsesScanCursor(kKeepShellAmortizeKC), "K-C cursor");

  PhysicsTelemetry pt{};
  pt.StreamerUpdateMs = 3.0;
  pt.StreamerUnloadMs = 4.0;
  pt.StreamerKeepShellMs = 2.0;
  pt.StreamerPrefetchAheadMs = 1.0;
  pt.UpdateStreamingMs = 12.0;
  const double slice =
      pt.StreamerUpdateMs + pt.StreamerUnloadMs + pt.StreamerKeepShellMs +
      pt.StreamerPrefetchAheadMs;
  Expect(slice <= pt.UpdateStreamingMs + 1e-6 ||
             pt.UpdateStreamingMs >= slice - 1.0,
         "slice sum fits UpdateStreaming phase (+eps for other work)");
  Expect(pt.StreamerUnloadMs >= 0.0 && pt.StreamerKeepShellMs >= 0.0,
         "telem fields present");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "StreamerAmortizePolicyTest: PASS\n";
  return 0;
}
