#include "Render/Mesh/MeshCaptureWorker.h"

namespace cutum
{

UMeshCaptureWorker::UMeshCaptureWorker(std::size_t thread_count)
{
  Pool = std::make_unique<UJobThreadPool>(thread_count > 0 ? thread_count : 1);
}

UMeshCaptureWorker::~UMeshCaptureWorker() { Shutdown(); }

void UMeshCaptureWorker::Shutdown()
{
  Accepting_.store(false, std::memory_order_release);
  Generation_.fetch_add(1, std::memory_order_acq_rel);
  if (Pool)
  {
    Pool->CancelPendingJobs();
    Pool->WaitIdle();
  }
  std::lock_guard<std::mutex> lock(Mutex);
  InFlight_.clear();
  Completed_.clear();
}

void UMeshCaptureWorker::Enqueue(ChunkMeshSnapshot band, WorkToken token,
                                 DependencyStamp deps)
{
  if (!kWorkerCaptureEnabled || !Pool ||
      !Accepting_.load(std::memory_order_acquire))
  {
    return;
  }
  const uint64_t job_id = NextJobId_.fetch_add(1, std::memory_order_relaxed);
  const uint64_t submit_generation =
      Generation_.load(std::memory_order_acquire);
  const glm::ivec3 coord = token.coord;
  WorkToken submit_token = token;
  submit_token.generation = submit_generation;
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (InFlight_.count(coord) > 0)
    {
      return;
    }
    Inflight inflight;
    inflight.source_revision = deps.content_revision;
    inflight.job_id = job_id;
    inflight.submit_generation = submit_generation;
    inflight.token = submit_token;
    inflight.deps = deps;
    InFlight_[coord] = inflight;
  }
  Pool->Enqueue([this, band = std::move(band), submit_token, deps, job_id,
                 submit_generation]() mutable
                {
                  CompletedCapture done;
                  done.token = submit_token;
                  done.deps = deps;
                  done.source_revision = deps.content_revision;
                  done.world_epoch = submit_token.world_epoch;
                  done.job_id = job_id;
                  done.snapshot = std::move(band);
                  std::lock_guard<std::mutex> lock(Mutex);
                  const auto it = InFlight_.find(submit_token.coord);
                  if (it == InFlight_.end() ||
                      it->second.job_id != job_id ||
                      it->second.submit_generation != submit_generation)
                  {
                    return;
                  }
                  InFlight_.erase(it);
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
  Generation_.fetch_add(1, std::memory_order_acq_rel);
  if (Pool)
  {
    Pool->CancelPendingJobs();
  }
  std::lock_guard<std::mutex> lock(Mutex);
  InFlight_.clear();
  Completed_.clear();
}

void UMeshCaptureWorker::CancelCoord(glm::ivec3 coord)
{
  Generation_.fetch_add(1, std::memory_order_acq_rel);
  std::lock_guard<std::mutex> lock(Mutex);
  InFlight_.erase(coord);
  for (auto it = Completed_.begin(); it != Completed_.end();)
  {
    if (it->token.coord == coord)
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
