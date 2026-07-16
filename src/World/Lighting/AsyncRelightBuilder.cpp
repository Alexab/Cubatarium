#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "World/Core/BlockWorld.h"
#include <mutex>
#include <thread>

namespace cutum
{

namespace
{
std::size_t ResolveRelightWorkerCount(std::size_t thread_count)
{
  return ComputeWorkerThreadCount(JobPoolKind::Relight, thread_count);
}

std::mutex gRelightCaptureMutex;
} // namespace

UAsyncRelightBuilder::UAsyncRelightBuilder(std::size_t thread_count)
    : WorkerCount(static_cast<int>(ResolveRelightWorkerCount(thread_count))),
      Pool(ResolveRelightWorkerCount(thread_count), "Relight")
{
}

void UAsyncRelightBuilder::Enqueue(UChunkRelightSnapshot snapshot,
                                   const UBlockRegistry &registry)
{
  const uint64_t submit_epoch = Epoch.load(std::memory_order_acquire);
  const uint64_t job_id = snapshot.GetJobId() > 0
                              ? snapshot.GetJobId()
                              : NextJobId.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  Pool.Enqueue([this, snapshot = std::move(snapshot), registryPtr = &registry,
                job_id, submit_epoch]() mutable
               {
                 RelightComputeResult result = snapshot.Compute(*registryPtr);
                 result.job_id = job_id;
                 result.submitEpoch = submit_epoch;
                 Completed.Push(std::move(result));
               });
}

void UAsyncRelightBuilder::EnqueueJob(const UBlockWorld &world,
                                      RelightJobSpec spec,
                                      const UBlockRegistry &registry)
{
  const uint64_t submit_epoch = Epoch.load(std::memory_order_acquire);
  const uint64_t job_id =
      spec.job_id > 0 ? spec.job_id
                      : NextJobId.fetch_add(1, std::memory_order_relaxed);
  spec.job_id = job_id;

  UChunkRelightSnapshot snapshot;
  {
    std::lock_guard<std::mutex> capture_lock(gRelightCaptureMutex);
    snapshot = UChunkRelightSnapshot::Capture(world, spec);
  }

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  Pool.Enqueue([this, snapshot = std::move(snapshot), registry = &registry,
                job_id, submit_epoch]() mutable
               {
                 RelightComputeResult result = snapshot.Compute(*registry);
                 result.job_id = job_id;
                 result.submitEpoch = submit_epoch;
                 Completed.Push(std::move(result));
               });
}

std::vector<RelightComputeResult>
UAsyncRelightBuilder::DrainCompleted(int max_per_frame)
{
  const std::size_t limit =
      max_per_frame > 0 ? static_cast<std::size_t>(max_per_frame) : 0;
  std::vector<RelightComputeResult> drained =
      limit > 0 ? Completed.DrainUpTo(limit) : std::vector<RelightComputeResult>{};
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  std::vector<RelightComputeResult> accepted;
  accepted.reserve(drained.size());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (RelightComputeResult &result : drained)
    {
      if (result.submitEpoch != current_epoch)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        InFlight.erase(result.job_id);
        continue;
      }
      InFlight.erase(result.job_id);
      accepted.push_back(std::move(result));
    }
  }
  return accepted;
}

bool UAsyncRelightBuilder::HasPendingWork() const
{
  if (!Completed.Empty())
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return !InFlight.empty();
}

int UAsyncRelightBuilder::GetInFlightCount() const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return static_cast<int>(InFlight.size());
}

void UAsyncRelightBuilder::WaitIdle() { Pool.WaitIdle(); }

bool UAsyncRelightBuilder::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncRelightBuilder::CancelPending()
{
  Epoch.fetch_add(1, std::memory_order_acq_rel);
  Pool.CancelPendingJobs();
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.clear();
  }
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  for (RelightComputeResult &result : Completed.DrainAll())
  {
    if (result.submitEpoch != current_epoch)
    {
      DiscardedLate.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

} // namespace cutum
