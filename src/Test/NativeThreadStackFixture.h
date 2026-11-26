#pragma once

#include "Core/Memory/NativeThreadStack.h"
#include "Test/IoCodecAllocationFixture.h"
#include <chrono>
#include <future>
#include <thread>
#include <utility>

namespace cutum::native_thread_stack_fixture
{
#ifdef _WIN32
using io_codec_fixture::Require;

class ParkedThread
{
public:
  template<class Work> explicit ParkedThread(Work work)
      : Thread([released = Release.get_future(), work = std::move(work)]() mutable {
          work(released);
        })
  {
  }
  ~ParkedThread()
  {
    Release.set_value();
    Thread.join();
  }

private:
  std::promise<void> Release;
  std::thread Thread;
};

constexpr std::size_t GrowthBytes = 128 * 1024;

__declspec(noinline) inline void HoldGrownStack(std::promise<NativeStackStorage> &ready,
                                              std::future<void> &release)
{
  volatile unsigned char pages[GrowthBytes];
  // Touch the full frame and leave it live while the other thread samples it.
  for (std::size_t page = 0; page < sizeof(pages); page += 4096)
    pages[page] = 1;
  ready.set_value(ReadCurrentNativeStack());
  release.wait();
  (void)pages[0];
}

inline nlohmann::json Run()
{
  using Clock = std::chrono::seconds;
  using Kind = NativeThreadKind;
  constexpr auto Main = static_cast<std::size_t>(Kind::Main);
  constexpr auto Worker = static_cast<std::size_t>(Kind::JobPool);
  constexpr auto Borrowed = static_cast<std::size_t>(Kind::SkyField);
  const auto initial = InspectNativeThreadStacks();
  DWORD handlesBefore{}, handlesAfter{};
  Require(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore) != 0, "observe handles before stack snapshots");
  NativeStackStorage shallow, deep;
  NativeThreadStacks baseline, held, retired;
  {
    const NativeThreadStackOwner main(Kind::Main, true);
    baseline = InspectNativeThreadStacks();
    Require(baseline.processThreadsObserved && baseline.rows[Main].active == initial.rows[Main].active + 1 &&
                baseline.rows[Main].sampled == baseline.rows[Main].active &&
                baseline.matched == initial.matched + 1,
            "join the running main stack to the Windows thread census");
    {
      std::promise<NativeStackStorage> stackReady;
      auto stack = stackReady.get_future();
      const ParkedThread owner([&](std::future<void> &released) {
        const NativeThreadStackOwner worker(Kind::JobPool, true);
        shallow = ReadCurrentNativeStack();
        const NativeThreadStackOwner borrowed(Kind::SkyField, true);
        HoldGrownStack(stackReady, released);
      });
      std::promise<void> foreignReady;
      auto foreign = foreignReady.get_future();
      const ParkedThread unregistered([&](std::future<void> &released) {
        foreignReady.set_value();
        released.wait();
      });
      Require(stack.wait_for(Clock(8)) == std::future_status::ready &&
                  foreign.wait_for(Clock(8)) == std::future_status::ready,
              "park the grown stack and an unregistered peer");
      deep = stack.get();
      held = InspectNativeThreadStacks();
      const auto &row = held.rows[Worker];
      Require(shallow.observed && deep.observed && deep.reserved == shallow.reserved &&
                  deep.committed >= shallow.committed + GrowthBytes / 2 && deep.guard > 0 &&
                  deep.guard <= deep.committed && deep.committed <= deep.reserved,
              "touching the stack adds committed pages within its original reservation");
      Require(held.processThreadsObserved && held.registered == baseline.registered + 1 &&
                  held.matched == baseline.matched + 1 && held.processThreads == baseline.processThreads + 2 &&
                  row.active == baseline.rows[Worker].active + 1 &&
                  row.sampled == baseline.rows[Worker].sampled + 1 &&
                  row.reserved == baseline.rows[Worker].reserved + deep.reserved &&
                  row.committed >= deep.committed && row.guard >= deep.guard &&
                  held.rows[Borrowed].borrowed == baseline.rows[Borrowed].borrowed + 1 &&
                  held.rows[Borrowed].active == baseline.rows[Borrowed].active,
              "count one registered worker, one foreign peer and one borrowed task without repeating a stack");
    }
    retired = InspectNativeThreadStacks();
    Require(retired.processThreadsObserved && retired.registered == baseline.registered &&
                retired.matched == baseline.matched && retired.processThreads == baseline.processThreads &&
                retired.rows[Worker].active == baseline.rows[Worker].active &&
                retired.rows[Worker].reserved == baseline.rows[Worker].reserved &&
                retired.rows[Worker].retired == baseline.rows[Worker].retired + 1 &&
                retired.rows[Worker].peakReserved >= held.rows[Worker].reserved,
            "joining retires each registered stack and preserves its reservation high water");
  }
  const auto exited = InspectNativeThreadStacks();
  Require(exited.registered == initial.registered && exited.rows[Main].retired == initial.rows[Main].retired + 1 &&
              GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) && handlesAfter == handlesBefore,
          "retire the main scope and every thread enumeration handle");
  return {{"status", "PASS"}, {"worker_reserved_bytes", deep.reserved},
      {"worker_commit_before_growth", shallow.committed}, {"worker_commit_in_grown_frame", deep.committed},
      {"worker_guard_bytes", deep.guard}, {"sampled_live_worker_bytes", held.rows[Worker].committed},
      {"process_threads_before", baseline.processThreads}, {"process_threads_held", held.processThreads},
      {"process_threads_after", retired.processThreads}, {"matched_before", baseline.matched},
      {"matched_held", held.matched}, {"matched_after", retired.matched},
      {"borrowed_stack_deduplicated", true}, {"retirement_observed", true}, {"handles_restored", true}};
}
#else
inline nlohmann::json Run() { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::native_thread_stack_fixture
