#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
#include <mutex>

namespace cutum
{

UAsyncRelightBuilder::UAsyncRelightBuilder(std::size_t thread_count)
    : Pool(thread_count)
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

} // namespace cutum
