#pragma once

#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Core/Jobs/JobThreadPool.h"
#include <atomic>
#include <chrono>
#include <glm/glm.hpp>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockWorld;

/// TD-ARCH-046: optional worker-side ChunkMeshSnapshot::Capture.
/// M2a: API + tests with kWorkerCaptureEnabled=false (default off in M2a).
/// M2b/M2c: enabled — main hot path uses TryGet + worker enqueue only.
class UMeshCaptureWorker
{
public:
  /// M2a gate: false until integration tests pass; M2b sets true.
  static constexpr bool kWorkerCaptureEnabled = true;

  explicit UMeshCaptureWorker(std::size_t thread_count = 1);

  bool IsEnabled() const { return kWorkerCaptureEnabled; }

  void Enqueue(const UBlockWorld &world, glm::ivec3 coord,
               uint64_t source_revision,
               ChunkMeshSnapshot::NeighborVisualDrawableFn neighbor_drawable,
               void *neighbor_drawable_ctx);

  /// Completed captures ready for CaptureStore commit (main thread only).
  struct CompletedCapture
  {
    glm::ivec3 coord{};
    uint64_t source_revision{0};
    ChunkMeshSnapshot snapshot;
  };

  std::vector<CompletedCapture> DrainCompleted(int max_per_frame);
  bool IsInFlight(glm::ivec3 coord) const;
  int GetInFlightCount() const;
  void CancelPending();

private:
  struct Inflight
  {
    uint64_t source_revision{0};
    uint64_t job_id{0};
  };

  std::unique_ptr<UJobThreadPool> Pool;
  mutable std::mutex Mutex;
  std::unordered_map<glm::ivec3, Inflight, IVec3Hash> InFlight_;
  std::vector<CompletedCapture> Completed_;
  std::atomic<uint64_t> NextJobId_{1};
};

} // namespace cutum
