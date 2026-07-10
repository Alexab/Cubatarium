#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;

class UAsyncRelightBuilder
{
public:
  explicit UAsyncRelightBuilder(std::size_t thread_count = 2);

  void Enqueue(UChunkRelightSnapshot snapshot, const UBlockRegistry &registry);
  std::vector<RelightComputeResult> DrainCompleted(int max_per_frame);
  bool HasPendingWork() const;
  void WaitIdle();

private:
  UJobThreadPool Pool;
  UCompletedJobQueue<RelightComputeResult> Completed;
  mutable std::mutex InFlightMutex;
  std::unordered_map<uint64_t, uint64_t> InFlight;
  uint64_t NextJobId{1};
};

} // namespace cutum
