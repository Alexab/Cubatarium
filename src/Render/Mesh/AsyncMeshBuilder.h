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
#include <memory>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class IUChunkMesher;

struct MeshBuildResult
{
  glm::ivec3 coord{0};
  std::vector<GreedyMeshBatch> batches;
  std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
  uint64_t sourceRevision{0};
  uint64_t jobId{0};
  uint64_t submitEpoch{0};
  /// P5: worker deferred eligible opaque extract to main (GL) thread.
  bool GpuExtractPending{false};
  std::unique_ptr<ChunkMeshSnapshot> PendingSnapshot;
};

class UAsyncMeshBuilder
{
public:
  explicit UAsyncMeshBuilder(std::size_t thread_count = 0);

  void SetMesher(IUChunkMesher *mesher) { Mesher = mesher; }
  IUChunkMesher *GetMesher() const { return Mesher; }

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
  /// Era53: enter ring checks async only near spawn (not global pool depth).
  bool HasInflightInHorizontalRadius(glm::ivec3 center_ground_chunk,
                                     int radius_chunks) const;
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
  /// Coords whose Completed mesh was dropped by overflow; remesh via Dirty.
  std::vector<glm::ivec3> TakeOverflowCoords();
  /// Coords discarded for stale epoch / jobId mismatch; remesh via Dirty.
  std::vector<glm::ivec3> TakeDiscardedCoords();

private:
  static constexpr int kPipelineSlotsPerWorker = 6;

  int WorkerCount{1};
  IUChunkMesher *Mesher{nullptr};
  UJobThreadPool Pool;
  UCompletedJobQueue<MeshBuildResult> Completed;
  mutable std::mutex InFlightMutex;
  std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> InFlight;
  std::atomic<uint64_t> NextJobId{1};
  std::atomic<uint64_t> Epoch{1};
  std::atomic<uint64_t> DiscardedLate{0};
  mutable std::mutex OverflowMutex;
  std::vector<glm::ivec3> OverflowCoords;
  mutable std::mutex DiscardedMutex;
  std::vector<glm::ivec3> DiscardedCoords;
};

} // namespace cutum
