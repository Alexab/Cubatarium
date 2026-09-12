#include "Core/Jobs/JobThreadPool.h"

#include <atomic>
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

/// Q2: normal destruction path (flight harness uses std::_Exit and skips this).
int main()
{
  std::atomic<int> ran{0};
  {
    cutum::UJobThreadPool pool(2, "ShutdownTest");
    for (int i = 0; i < 8; ++i)
    {
      pool.Enqueue([&ran]()
                   {
                     ran.fetch_add(1, std::memory_order_relaxed);
                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
                   });
    }
    Expect(pool.WaitIdleFor(std::chrono::milliseconds(2000)),
           "pool drains before destroy");
    Expect(ran.load() == 8, "all jobs ran");
    pool.CancelPendingJobs();
  } // destructor joins workers — not _Exit

  {
    cutum::UJobThreadPool pool(1, "ShutdownCancel");
    pool.SetMaxPendingJobCount(64);
    for (int i = 0; i < 32; ++i)
    {
      pool.Enqueue([]()
                   { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
    }
    pool.CancelPendingJobs();
    Expect(pool.WaitIdleFor(std::chrono::milliseconds(2000)),
           "cancel + idle before destroy");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "NormalShutdownTest: PASS\n";
  return 0;
}
