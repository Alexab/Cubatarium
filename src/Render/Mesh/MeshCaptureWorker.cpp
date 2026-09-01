#include "Render/Mesh/MeshCaptureWorker.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

UMeshCaptureWorker::UMeshCaptureWorker(std::size_t thread_count)
{
  Pool = std::make_unique<UJobThreadPool>(thread_count > 0 ? thread_count : 1);
}

void UMeshCaptureWorker::Enqueue(
    const UBlockWorld &world, glm::ivec3 coord, uint64_t source_revision,
    ChunkMeshSnapshot::NeighborVisualDrawableFn neighbor_drawable,
    void *neighbor_drawable_ctx)
{
  if (!kWorkerCaptureEnabled || !Pool)
  {
    return;
  }
  const uint64_t job_id = NextJobId_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (InFlight_.count(coord) > 0)
    {
      return;
    }
    InFlight_[coord] = Inflight{source_revision, job_id};
  }
  const UBlockWorld *world_ptr = &world;
  Pool->Enqueue(
      [this, world_ptr, coord, source_revision, neighbor_drawable,
       neighbor_drawable_ctx, job_id]()
      {
        ChunkMeshSnapshot snap = ChunkMeshSnapshot::Capture(
            *world_ptr, coord, source_revision, neighbor_drawable,
            neighbor_drawable_ctx);
        CompletedCapture done;
        done.coord = coord;
        done.source_revision = source_revision;
        done.snapshot = std::move(snap);
        std::lock_guard<std::mutex> lock(Mutex);
        const auto it = InFlight_.find(coord);
        if (it != InFlight_.end() && it->second.job_id == job_id &&
            it->second.source_revision == source_revision)
        {
          InFlight_.erase(it);
          Completed_.push_back(std::move(done));
        }
      });
}

std::vector<UMeshCaptureWorker::CompletedCapture>
UMeshCaptureWorker::DrainCompleted(int max_per_frame)
{
  std::vector<CompletedCapture> out;
  std::lock_guard<std::mutex> lock(Mutex);
  const int n = std::min(max_per_frame, static_cast<int>(Completed_.size()));
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    out.push_back(std::move(Completed_.front()));
    Completed_.erase(Completed_.begin());
  }
  return out;
}

bool UMeshCaptureWorker::IsInFlight(glm::ivec3 coord) const
{
  std::lock_guard<std::mutex> lock(Mutex);
  return InFlight_.count(coord) > 0;
}

int UMeshCaptureWorker::GetInFlightCount() const
{
  std::lock_guard<std::mutex> lock(Mutex);
  return static_cast<int>(InFlight_.size());
}

void UMeshCaptureWorker::CancelPending()
{
  std::lock_guard<std::mutex> lock(Mutex);
  InFlight_.clear();
  Completed_.clear();
}

} // namespace cutum
