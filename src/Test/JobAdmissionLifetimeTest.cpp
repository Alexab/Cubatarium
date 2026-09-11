#include "Core/Jobs/JobThreadPool.h"
#include "Core/Jobs/PipelineAdmission.h"
#include <future>
#include <iostream>
#include <limits>
#include <memory>

// Production pool with only the thread-name diagnostic stubbed out.
namespace cutum
{
void CubatariumSetWorkerJobKind(const char *) {}
} // namespace cutum

int main()
{
  using namespace cutum;
  int failures = 0;
  auto check = [&](bool ok, const char *what)
  {
    if (!ok)
    {
      std::cerr << "FAIL: " << what << '\n';
      ++failures;
    }
  };
  auto &admission = UPipelineAdmission::Get();
  admission.SetSnapshotCap(64);
  check(admission.TryAcquireSnapshotBytes(64), "reserve snapshot");
  check(!admission.TryAcquireSnapshotBytes(std::numeric_limits<size_t>::max()),
        "overflow cannot bypass cap");
  UJobThreadPool pool(1);
  pool.SetMaxPendingJobCount(1);
  std::promise<void> started, unblock;
  auto gate = unblock.get_future().share();
  pool.Enqueue(
      [&]
      {
        started.set_value();
        gate.wait();
      });
  started.get_future().wait();
  auto lease = std::make_shared<UPipelineCreditGuard>(
      PipelineCreditKind::Snapshot, 64, true);
  check(pool.TryEnqueue([lease] {}), "queued lease");
  lease.reset();
  check(!pool.TryEnqueue([] {}), "explicit bounded rejection");
  pool.CancelPendingJobs();
  check(admission.SnapshotPendingBytes() == 0, "cancel releases queued lease");
  int ran = 0;
  pool.Enqueue([&] { ++ran; });
  pool.Enqueue([&] { ++ran; });
  unblock.set_value();
  pool.WaitIdle();
  check(ran == 2, "legacy enqueue never silently loses work at cap");
  admission.SetResultCap(8);
  check(admission.TryAcquireResultBytes(8), "result reserve");
  {
    UPipelineCreditGuard result(PipelineCreditKind::Result, 8, true);
    check(!admission.TryAcquireResultBytes(1), "result rejection");
    UPipelineCreditGuard rejected(PipelineCreditKind::Result, 1, false);
  }
  check(admission.ResultPendingBytes() == 0, "no unowned credit release");
  UCompletedJobQueue<int> completed;
  completed.SetCapacity(3);
  completed.Push(10);
  completed.Push(20);
  completed.Push(30);
  const auto evicted = completed.SetCapacity(1);
  check(evicted == std::vector<int>({10, 20}),
        "shrinking reports every evicted result for token cleanup/retry");
  check(completed.DrainAll() == std::vector<int>({30}),
        "shrinking retains newest completion");
  return failures ? 1 : 0;
}
