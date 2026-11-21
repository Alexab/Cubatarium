#include "Core/Jobs/JobThreadPool.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Core/Memory/SharedCountingAllocator.h"
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
  {
    auto memory=std::make_shared<UConcurrentAllocationCountingResource>();
    struct alignas(64) Payload { int value{42}; };
    std::weak_ptr<Payload> weak;
    {
      auto payload=std::allocate_shared<Payload>(USharedCountingAllocator<Payload>{memory});
      check(reinterpret_cast<uintptr_t>(payload.get())%alignof(Payload)==0,"counted control allocation alignment");
      weak=payload;
      check(payload->value==42 && memory->GetSnapshot().live_bytes>=sizeof(Payload),
          "actual allocate_shared payload and control bytes counted");
    }
    check(weak.expired() && memory->GetSnapshot().live_bytes>0,"weak owner keeps control allocation counted");
    weak.reset();
    check(memory->GetSnapshot().live_bytes==0,"last weak owner releases counted allocation");
    std::vector<std::thread> producers;
    for(int worker=0;worker<4;++worker) producers.emplace_back([memory] {
      for(int n=0;n<1000;++n) {
        auto value=std::allocate_shared<Payload>(USharedCountingAllocator<Payload>{memory});
      }
    });
    for(auto &worker:producers) worker.join();
    check(memory->GetSnapshot().live_bytes==0 && memory->GetSnapshot().live_allocations==0 &&
        memory->GetSnapshot().peak_bytes>0,"concurrent control allocation retires all bytes");
    auto owner=std::allocate_shared<Payload>(USharedCountingAllocator<Payload>{memory});
    std::weak_ptr<UConcurrentAllocationCountingResource> resource_lifetime=memory;
    memory.reset();
    check(!resource_lifetime.expired() && owner->value==42,"allocator retains resource after outer owner retires");
    owner.reset();
    check(resource_lifetime.expired(),"last allocation releases retained resource");
  }
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
  // S6: work-slot envelope — denial must not leave dangling slots.
  admission.SetWorkSlotCap(1);
  check(admission.TryAcquireWorkSlot(), "work slot acquire");
  check(!admission.TryAcquireWorkSlot(), "work slot denial at cap");
  admission.ReleaseWorkSlot();
  check(admission.WorkSlotsInUse() == 0, "work slot released");
  check(admission.TryAcquireWorkSlot(), "work slot reacquire after release");
  admission.ReleaseWorkSlot();
  // Audit16 S6 stress: fill snapshot+result+slots to cap; deny; cancel→0.
  {
    admission.SetSnapshotCap(128);
    admission.SetResultCap(64);
    admission.SetWorkSlotCap(4);
    check(admission.TryAcquireSnapshotBytes(128), "stress snapshot fill");
    check(!admission.TryAcquireSnapshotBytes(1), "stress snapshot deny at cap");
    check(admission.SnapshotPendingBytes() == 128, "stress snapshot pending==cap");
    check(admission.TryAcquireResultBytes(64), "stress result fill");
    check(!admission.TryAcquireResultBytes(1), "stress result deny at cap");
    check(admission.ResultPendingBytes() == 64, "stress result pending==cap");
    int slots = 0;
    while (admission.TryAcquireWorkSlot())
      ++slots;
    check(slots == 4, "stress work slots filled to cap");
    check(!admission.TryAcquireWorkSlot(), "stress work slot deny");
    check(admission.WorkSlotsInUse() == 4, "stress slots in use==cap");
    // Dual-lane style: release one FM-like and one remesh-like credit cycle.
    admission.ReleaseWorkSlot();
    check(admission.TryAcquireWorkSlot(), "stress FM credit cycle");
    admission.ReleaseWorkSlot();
    check(admission.TryAcquireWorkSlot(), "stress remesh credit cycle");
    while (admission.WorkSlotsInUse() > 0)
      admission.ReleaseWorkSlot();
    admission.ReleaseSnapshotBytes(128);
    admission.ReleaseResultBytes(64);
    check(admission.SnapshotPendingBytes() == 0, "stress snapshot credits→0");
    check(admission.ResultPendingBytes() == 0, "stress result credits→0");
    check(admission.WorkSlotsInUse() == 0, "stress work slots→0");
  }
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
  // Selective mesh cancellation must release only the cancelled queued lease;
  // the protected queued closure and running worker remain executable.
  {
    UJobThreadPool selective(1);
    admission.SetSnapshotCap(64);
    std::promise<void> entered, release;
    auto unblock_selective = release.get_future().share();
    selective.Enqueue([&] { entered.set_value(); unblock_selective.wait(); });
    entered.get_future().wait();
    int ran_mask = 0;
    const auto enqueue_tagged = [&](uint64_t id, int bit) {
      check(admission.TryAcquireSnapshotBytes(16), "selective snapshot reserve");
      auto credit = std::make_shared<UPipelineCreditGuard>(
          PipelineCreditKind::Snapshot, 16, true);
      check(selective.TryEnqueue([&, credit, bit] { ran_mask |= bit; }, id),
            "selective tagged job accepted");
    };
    enqueue_tagged(701, 1);
    enqueue_tagged(702, 2);
    check(selective.CancelPendingJobsIf([](uint64_t id) { return id == 701; }) == 1,
          "cancel exactly the outside tag");
    check(selective.GetPendingJobCount() == 1 &&
              admission.SnapshotPendingBytes() == 16,
          "cancel releases outside credit and preserves protected credit");
    release.set_value();
    selective.WaitIdle();
    check(ran_mask == 2 && admission.SnapshotPendingBytes() == 0,
          "protected job executes; idle includes closure credit release");
  }
  return failures ? 1 : 0;
}
