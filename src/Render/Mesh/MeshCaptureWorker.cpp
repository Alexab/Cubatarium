#include "Render/Mesh/MeshCaptureWorker.h"

namespace cutum
{

UMeshCaptureWorker::UMeshCaptureWorker(std::size_t thread_count)
{
  Pool = std::make_unique<UJobThreadPool>(thread_count > 0 ? thread_count : 1);
}

void UMeshCaptureWorker::Enqueue(ChunkMeshSnapshot band, glm::ivec3 coord,
                                 uint64_t source_revision)
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
  Pool->Enqueue([this, band = std::move(band), coord, source_revision,
                 job_id]() mutable
                {
                  CompletedCapture done;
                  done.coord = coord;
                  done.source_revision = source_revision;
                  done.snapshot = std::move(band);
                  std::lock_guard<std::mutex> lock(Mutex);
                  const auto it = InFlight_.find(coord);
                  if (it != InFlight_.end() && it->second.job_id == job_id)
                  {
                    InFlight_.erase(it);
                  }
                  Completed_.push_back(std::move(done));
                });
}

void UMeshCaptureWorker::PumpUntilIdle(std::chrono::milliseconds max_wait)
{
  if (!Pool || max_wait.count() <= 0)
  {
    return;
  }
  (void)Pool->WaitIdleFor(max_wait);
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

void UMeshCaptureWorker::CancelCoord(glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(Mutex);
  InFlight_.erase(coord);
  for (auto it = Completed_.begin(); it != Completed_.end();)
  {
    if (it->coord == coord)
    {
      it = Completed_.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

} // namespace cutum
