#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

class UAsyncRelightBuilder
{
public:
  explicit UAsyncRelightBuilder(std::size_t thread_count = 2);

  void Enqueue(UChunkRelightSnapshot snapshot, const UBlockRegistry &registry);
  void EnqueueJob(const UBlockWorld &world, RelightJobSpec spec,
                  const UBlockRegistry &registry);
  std::vector<RelightComputeResult> DrainCompleted(int max_per_frame);
  bool HasPendingWork() const;
  int GetInFlightCount() const;
  int GetWorkerCount() const { return WorkerCount; }
  int GetMaxPipelineDepth() const
  {
    return WorkerCount * kPipelineSlotsPerWorker;
  }
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();

private:
  static constexpr int kPipelineSlotsPerWorker = 8;

  int WorkerCount{1};
  UJobThreadPool Pool;
  UCompletedJobQueue<RelightComputeResult> Completed;
  mutable std::mutex InFlightMutex;
  std::unordered_map<uint64_t, uint64_t> InFlight;
  uint64_t NextJobId{1};
};

} // namespace cutum
