#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <mutex>
#include <thread>

namespace cutum
{

namespace
{
std::size_t ResolveRelightWorkerCount(std::size_t thread_count)
{
  if (thread_count == 0)
  {
    const std::size_t hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 1;
  }
  return std::max<std::size_t>(1, thread_count);
}

std::mutex gRelightCaptureMutex;
} // namespace

UAsyncRelightBuilder::UAsyncRelightBuilder(std::size_t thread_count)
    : WorkerCount(static_cast<int>(ResolveRelightWorkerCount(thread_count))),
      Pool(ResolveRelightWorkerCount(thread_count))
{
}

void UAsyncRelightBuilder::Enqueue(UChunkRelightSnapshot snapshot,
                                   const UBlockRegistry &registry)
{
  const uint64_t job_id =
      snapshot.GetJobId() > 0 ? snapshot.GetJobId() : NextJobId++;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  Pool.Enqueue([this, snapshot = std::move(snapshot), registryPtr = &registry,
                job_id]() mutable
               {
                 RelightComputeResult result = snapshot.Compute(*registryPtr);
                 result.job_id = job_id;
                 Completed.Push(std::move(result));
               });
}

void UAsyncRelightBuilder::EnqueueJob(const UBlockWorld &world,
                                      RelightJobSpec spec,
                                      const UBlockRegistry &registry)
{
  const uint64_t job_id =
      spec.job_id > 0 ? spec.job_id : NextJobId++;
  spec.job_id = job_id;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  Pool.Enqueue([this, &world, spec = std::move(spec), &registry,
                job_id]() mutable
               {
                 UChunkRelightSnapshot snapshot;
                 {
                   std::lock_guard<std::mutex> capture_lock(gRelightCaptureMutex);
                   snapshot = UChunkRelightSnapshot::Capture(world, spec);
                 }
                 RelightComputeResult result = snapshot.Compute(registry);
                 result.job_id = job_id;
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
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (const RelightComputeResult &result : drained)
    {
      InFlight.erase(result.job_id);
    }
  }
  return drained;
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
  Pool.CancelPendingJobs();
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.clear();
  }
  (void)Completed.DrainAll();
}

} // namespace cutum
