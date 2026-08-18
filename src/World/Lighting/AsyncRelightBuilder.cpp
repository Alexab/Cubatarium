#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include <algorithm>
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
  const int slots = URuntimeTuning::Get().RelightCompletedSlots;
  const std::size_t cap =
      slots > 0 ? static_cast<std::size_t>(slots)
                : static_cast<std::size_t>(WorkerCount * kPipelineSlotsPerWorker);
  Completed.SetCapacity(cap);
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

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();

  Pool.Enqueue([this, snapshot = std::move(snapshot), registryPtr = &registry,
                catalogKeep = std::move(catalogKeep), job_id,
                submit_epoch]() mutable
               {
                 (void)catalogKeep;
                 RelightComputeResult result = snapshot.Compute(*registryPtr);
                 result.job_id = job_id;
                 result.submitEpoch = submit_epoch;
                 RelightComputeResult dropped;
                 if (Completed.PushDropOldest(std::move(result), &dropped))
                 {
                   NoteCompletedOverflow(std::move(dropped));
                 }
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
  LastCaptureFullN = snapshot.GetCapturedFullChunks();
  LastCaptureNeighborLightN = snapshot.GetCapturedNeighborLightChunks();

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();

  Pool.Enqueue([this, snapshot = std::move(snapshot), registry = &registry,
                catalogKeep = std::move(catalogKeep), job_id,
                submit_epoch]() mutable
               {
                 (void)catalogKeep;
                 RelightComputeResult result = snapshot.Compute(*registry);
                 result.job_id = job_id;
                 result.submitEpoch = submit_epoch;
                 RelightComputeResult dropped;
                 if (Completed.PushDropOldest(std::move(result), &dropped))
                 {
                   NoteCompletedOverflow(std::move(dropped));
                 }
               });
}

std::vector<RelightComputeResult>
UAsyncRelightBuilder::DrainCompleted(int max_per_frame)
{
  (void)max_per_frame;
  // DrainAll so stale epoch results free light arrays immediately instead of
  // peeling DrainUpTo while new Captures keep enqueueing.
  std::vector<RelightComputeResult> drained = Completed.DrainAll();
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
        {
          std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
          DiscardedSources.insert(DiscardedSources.end(),
                                  result.source_block_positions.begin(),
                                  result.source_block_positions.end());
        }
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
      std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
      DiscardedSources.insert(DiscardedSources.end(),
                              result.source_block_positions.begin(),
                              result.source_block_positions.end());
    }
  }
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeDiscardedSourcePositions()
{
  std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
  std::vector<glm::ivec3> out;
  out.swap(DiscardedSources);
  return out;
}

void UAsyncRelightBuilder::NoteCompletedOverflow(RelightComputeResult &&dropped)
{
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.erase(dropped.job_id);
  }
  std::vector<glm::ivec3> sources = std::move(dropped.source_block_positions);
  if (sources.empty())
  {
    for (const RelightChunkLightData &chunk : dropped.chunks)
    {
      sources.push_back(glm::ivec3(chunk.coord.x * CHUNK_SIZE,
                                   chunk.coord.y * CHUNK_SIZE,
                                   chunk.coord.z * CHUNK_SIZE));
    }
  }
  std::lock_guard<std::mutex> olock(OverflowMutex);
  OverflowSources.insert(OverflowSources.end(), sources.begin(), sources.end());
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeOverflowSourcePositions()
{
  std::lock_guard<std::mutex> olock(OverflowMutex);
  std::vector<glm::ivec3> out;
  out.swap(OverflowSources);
  return out;
}

} // namespace cutum
