#include "Core/FrameDeadline.h"
#include "World/Chunks/ChunkStreamer.h"

#include <chrono>
#include <iostream>
#include <thread>

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
  using cutum::StreamerLoadLoopShouldBreak;
  using cutum::UFrameDeadline;
  using cutum::kStreamerEnsureSyncSubColumns;

  Expect(kStreamerEnsureSyncSubColumns == 32,
         "Ensure sync budget is 32 not full column 256");
  Expect(kStreamerEnsureSyncSubColumns < 256,
         "Ensure sync budget never full-column");

  Expect(StreamerLoadLoopShouldBreak(true, 0, 8, 0.0, 8.0, 16.0),
         "Exhausted: break before Ensure");
  Expect(!StreamerLoadLoopShouldBreak(false, 0, 8, 0.0, 8.0, 16.0),
         "fresh: allow Ensure");
  Expect(StreamerLoadLoopShouldBreak(false, 8, 8, 1.0, 8.0, 16.0),
         "max ops: break");
  Expect(StreamerLoadLoopShouldBreak(false, 1, 8, 9.0, 8.0, 16.0),
         "soft budget with ops: break");
  Expect(StreamerLoadLoopShouldBreak(false, 0, 8, 17.0, 8.0, 16.0),
         "hard wall: break even with 0 ops");

  auto &dl = UFrameDeadline::Get();
  dl.BeginFrame(1.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Expect(dl.Exhausted(), "deadline exhausted");
  Expect(StreamerLoadLoopShouldBreak(dl.Exhausted(), 0, 8, 0.0, 8.0, 16.0),
         "Exhausted maps to load-loop break (Ensure not called)");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "StreamerDeadlinePolicyTest: PASS\n";
  return 0;
}
