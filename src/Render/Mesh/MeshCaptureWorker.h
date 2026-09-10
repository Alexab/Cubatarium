#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "World/Streaming/WorkToken.h"
#include "Core/Jobs/JobThreadPool.h"
#include <atomic>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{

/// TD-ARCH-046: optional async capture hop (M10 default = off).
/// Main ReadChunkBandForCapture → Commit directly; worker is passthrough only.
class UMeshCaptureWorker
{
public:
  /// M10: default path commits on main; enable only for integration tests.
  static constexpr bool kWorkerCaptureEnabled = false;

  explicit UMeshCaptureWorker(std::size_t thread_count = 1);
  ~UMeshCaptureWorker();

  UMeshCaptureWorker(const UMeshCaptureWorker &) = delete;
  UMeshCaptureWorker &operator=(const UMeshCaptureWorker &) = delete;

  bool IsEnabled() const { return kWorkerCaptureEnabled; }

  void Enqueue(ChunkMeshSnapshot band, WorkToken token, DependencyStamp deps);

  struct CompletedCapture
  {
    WorkToken token{};
    DependencyStamp deps{};
    uint64_t source_revision{0};
    uint64_t world_epoch{0};
    uint64_t job_id{0};
    ChunkMeshSnapshot snapshot;
  };

  std::vector<CompletedCapture> DrainCompleted(int max_per_frame);
  void PumpUntilIdle(std::chrono::milliseconds max_wait);
  bool IsInFlight(glm::ivec3 coord) const;
  int GetInFlightCount() const;
  void CancelPending();
  void CancelCoord(glm::ivec3 coord);
  uint64_t Generation() const
  {
    return Generation_.load(std::memory_order_acquire);
  }

private:
  struct Inflight
  {
    uint64_t source_revision{0};
    uint64_t job_id{0};
    uint64_t submit_generation{0};
    WorkToken token{};
    DependencyStamp deps{};
  };

  void Shutdown();

  mutable std::mutex Mutex;
  std::unordered_map<glm::ivec3, Inflight, IVec3Hash> InFlight_;
  std::vector<CompletedCapture> Completed_;
  std::atomic<uint64_t> NextJobId_{1};
  std::atomic<uint64_t> Generation_{1};
  std::atomic<bool> Accepting_{true};
  /// Pool last — destroyed first while callback state remains valid (M08).
  std::unique_ptr<UJobThreadPool> Pool;
};

} // namespace cutum
