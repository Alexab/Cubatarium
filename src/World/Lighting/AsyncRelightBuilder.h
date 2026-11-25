#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/LightSampleDeliveryBudget.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct AsyncRelightEnqueueTimings
{
  double capture_lock_wait_ms{0.0};
  double snapshot_copy_ms{0.0};
  double dependency_stamp_ms{0.0};
  double submit_setup_ms{0.0};
  double queue_submit_ms{0.0};
};

class UAsyncRelightBuilder
{
public:
  explicit UAsyncRelightBuilder(std::size_t thread_count = 2);

  void Enqueue(UChunkRelightSnapshot snapshot, const UBlockRegistry &registry);
  AsyncRelightEnqueueTimings EnqueueJob(const UBlockWorld &world,
                                        RelightJobSpec spec,
                                        const UBlockRegistry &registry);
  std::vector<RelightComputeResult> DrainCompleted(int max_per_frame);
  bool HasPendingWork() const;
  int GetInFlightCount() const;
  bool HasInFlightJob(uint64_t job_id) const;
  /// Diagnostic watchdog input: age of the oldest in-flight submission
  /// (steady ms), 0 when nothing is in flight.
  double GetOldestInFlightAgeMs() const;
  /// Main thread only. Includes recent complete-input deliveries and current
  /// in-flight/completed age; this is a bounded forecast, not readiness.
  double GetLightSampleHorizonSeconds() const;
  std::size_t GetQueuedJobCount() const { return Pool.GetPendingJobCount(); }
  std::size_t GetRunningJobCount() const { return Pool.GetActiveJobCount(); }
  int GetWorkerCount() const { return WorkerCount; }
  template<class Inspector> void InspectJobPools(Inspector &out) const
  {out.Pool("relight",Pool,4);}
  int GetMaxPipelineDepth() const
  {
    return WorkerCount * kPipelineSlotsPerWorker;
  }
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();
  /// Transfers exact retry obligations once, including jobs cancelled in queue.
  std::vector<RelightJobSpec> TakeDiscardedJobs();
  std::vector<RelightJobSpec> TakeOverflowJobs();
  /// Source block positions from jobs discarded by CancelPending / late epoch.
  std::vector<glm::ivec3> TakeDiscardedSourcePositions();
  uint64_t GetDiscardedLateCount() const
  {
    return DiscardedLate.load(std::memory_order_relaxed);
  }
  uint64_t SubmitEpoch() const
  {
    return Epoch.load(std::memory_order_acquire);
  }
  std::size_t GetCompletedSize() const { return Completed.Size(); }
  std::size_t GetCompletedCapacity() const { return Completed.Capacity(); }
  uint64_t GetCompletedDiscardedOverflow() const
  {
    return Completed.DiscardedOverflow();
  }
  void SetCompletedCapacity(std::size_t cap)
  {
    for (auto &dropped : Completed.SetCapacity(cap))
      NoteCompletedOverflow(std::move(dropped));
  }
  /// Source block positions from Completed overflow drops (re-enqueue relight).
  std::vector<glm::ivec3> TakeOverflowSourcePositions();

  int GetLastCaptureFullChunks() const { return LastCaptureFullN; }
  int GetLastCaptureNeighborLightChunks() const
  {
    return LastCaptureNeighborLightN;
  }

private:
  friend struct AsyncRelightLifecycleTestAccess;
  void NoteCompletedOverflow(RelightComputeResult &&dropped);
  void ReturnUnsubmittedJob(uint64_t job_id, uint64_t submit_epoch);

  static constexpr int kPipelineSlotsPerWorker = 8;

  int WorkerCount{1};
  // Completed must outlive Pool (destroy order = reverse declaration).
  UCompletedJobQueue<RelightComputeResult> Completed;
  mutable std::mutex InFlightMutex;
  /// job_id -> steady ms at submit (value doubles as the watchdog age source).
  std::unordered_map<uint64_t, int64_t> InFlight;
  struct RetryObligation
  {
    uint64_t epoch;
    RelightJobSpec spec;
  };
  /// Guarded by InFlightMutex; survives until delivery, cancel or overflow.
  std::unordered_map<uint64_t, RetryObligation> RetryObligations;
  LightSampleDeliveryBudget SampleDeliveryBudget;
  std::atomic<uint64_t> NextJobId{1};
  std::atomic<uint64_t> Epoch{1};
  std::atomic<uint64_t> DiscardedLate{0};
  mutable std::mutex DiscardedSourcesMutex;
  std::vector<RelightJobSpec> DiscardedJobs;
  mutable std::mutex OverflowMutex;
  std::vector<RelightJobSpec> OverflowJobs;
  int LastCaptureFullN{0};
  int LastCaptureNeighborLightN{0};
  // Join before destroying any state captured by callbacks.
  UJobThreadPool Pool;
};

} // namespace cutum
