#include "Core/FrameDeadline.h"

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
  auto &deadline = cutum::UFrameDeadline::Get();
  deadline.BeginFrame(50.0);
  Expect(deadline.BudgetMs() == 50.0, "budget set");
  Expect(deadline.RemainingMs() > 40.0, "remaining near budget");
  Expect(!deadline.Exhausted(), "not exhausted");

  deadline.BeginFrame(1.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Expect(deadline.Exhausted(), "exhausted after sleep");
  Expect(deadline.RemainingMs() == 0.0, "remaining clamped to 0");

  deadline.BeginFrame(0.0);
  Expect(!deadline.Exhausted(), "zero budget means unlimited");
  Expect(!cutum::UFrameDeadline::ShouldDeferProducer(false),
         "unlimited: non-critical not deferred");
  Expect(!cutum::UFrameDeadline::ShouldDeferProducer(true),
         "unlimited: FirstMesh not deferred");

  deadline.BeginFrame(1.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Expect(cutum::UFrameDeadline::ShouldDeferProducer(false),
         "exhausted: non-critical deferred");
  Expect(!cutum::UFrameDeadline::ShouldDeferProducer(true),
         "exhausted: FirstMesh never hard-killed");

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "FrameDeadlineTest: PASS\n";
  return 0;
}
