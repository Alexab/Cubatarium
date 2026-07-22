#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <atomic>
#include <chrono>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;

struct MeshBuildResult
{
  glm::ivec3 coord{0};
  std::vector<GreedyMeshBatch> batches;
  std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
  uint64_t sourceRevision{0};
  uint64_t jobId{0};
  uint64_t submitEpoch{0};
};

class UAsyncMeshBuilder
{
public:
  explicit UAsyncMeshBuilder(std::size_t thread_count = 0);

  void Enqueue(ChunkMeshSnapshot snapshot, UBlockRegistry &registry);
  std::vector<MeshBuildResult> DrainCompleted(int maxPerFrame);
  bool IsInFlight(glm::ivec3 coord) const;
  int GetInFlightCount() const;
  int GetWorkerCount() const { return WorkerCount; }
  int GetMaxPipelineDepth() const
  {
    return WorkerCount * kPipelineSlotsPerWorker;
  }
  bool HasPendingWork() const;
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();
  /// Drop in-flight tracking for a coord (e.g. chunk unloaded); late results are ignored.
  void ForgetInflight(glm::ivec3 coord);
  uint64_t GetDiscardedLateCount() const
  {
    return DiscardedLate.load(std::memory_order_relaxed);
  }
  std::size_t GetCompletedSize() const { return Completed.Size(); }
  std::size_t GetCompletedCapacity() const { return Completed.Capacity(); }
  uint64_t GetCompletedDiscardedOverflow() const
  {
    return Completed.DiscardedOverflow();
  }
  void SetCompletedCapacity(std::size_t cap) { Completed.SetCapacity(cap); }

private:
  static constexpr int kPipelineSlotsPerWorker = 6;

  int WorkerCount{1};
  UJobThreadPool Pool;
  UCompletedJobQueue<MeshBuildResult> Completed;
  mutable std::mutex InFlightMutex;
  std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> InFlight;
  std::atomic<uint64_t> NextJobId{1};
  std::atomic<uint64_t> Epoch{1};
  std::atomic<uint64_t> DiscardedLate{0};
};

} // namespace cutum
