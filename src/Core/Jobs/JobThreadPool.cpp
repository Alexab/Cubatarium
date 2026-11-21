#include "Core/Jobs/JobThreadPool.h"
#include "App/Platform/Log.h"
#include "Core/Jobs/JobThreadBudget.h"
#include <algorithm>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cutum
{

UJobThreadPool::StackStorageSnapshot UJobThreadPool::GetCurrentThreadStackStorage()
{
  StackStorageSnapshot result;
#if defined(_WIN32)
  unsigned char marker=0;
  MEMORY_BASIC_INFORMATION region{};
  if(!VirtualQuery(&marker,&region,sizeof(region))) return result;
  const auto base=region.AllocationBase;
  auto cursor=reinterpret_cast<uintptr_t>(base);
  for(;;) {
    if(!VirtualQuery(reinterpret_cast<void *>(cursor),&region,sizeof(region))) return result;
    if(region.AllocationBase!=base) { result.observed=true; return result; }
    if(region.State==MEM_RESERVE || region.State==MEM_COMMIT)
      result.reservation_bytes+=region.RegionSize;
    if(region.State==MEM_COMMIT) result.committed_bytes+=region.RegionSize;
    const auto next=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
    if(next<=cursor) return result;
    cursor=next;
  }
#else
  return result;
#endif
}

UJobThreadPool::UJobThreadPool(std::size_t threadCount,
                               const char *worker_job_kind)
    : WorkerJobKind(worker_job_kind ? worker_job_kind : "Worker")
{
  if (threadCount == 0)
  {
    threadCount = ComputeWorkerThreadCount(JobPoolKind::ChunkGeneration);
  }
  Workers.reserve(threadCount);
  for (std::size_t i = 0; i < threadCount; ++i)
  {
    Workers.emplace_back([this] { WorkerLoop(); });
  }
}

UJobThreadPool::~UJobThreadPool()
{
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    if (Stop && Workers.empty())
    {
      return;
    }
    Stop = true;
  }
  QueueCv.notify_all();
  for (std::thread &worker : Workers)
  {
    if (worker.joinable())
    {
      worker.join();
    }
  }
  Workers.clear();
}

void UJobThreadPool::ShutdownForProcessExit(
    const std::chrono::milliseconds timeout)
{
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    if (Stop && Workers.empty())
    {
      return;
    }
    Stop = true;
    Jobs.clear();
  }
  QueueCv.notify_all();
  const bool idle = WaitIdleFor(timeout);
  for (std::thread &worker : Workers)
  {
    if (!worker.joinable())
    {
      continue;
    }
    if (idle)
    {
      worker.join();
    }
    else
    {
      // Process is exiting; do not block forever on carve/populate.
      worker.detach();
    }
  }
  Workers.clear();
}

void UJobThreadPool::Enqueue(std::function<void()> job)
{
  // Legacy callers (including persistence) have no retry protocol. Preserve
  // their lossless contract; bounded producers must use TryEnqueue explicitly.
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    Jobs.push_back({std::move(job), 0});
  }
  QueueCv.notify_one();
}

bool UJobThreadPool::TryEnqueue(std::function<void()> job, uint64_t tag)
{
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    if (Stop || Jobs.size() >= MaxPendingJobs)
    {
      RejectedEnqueues.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    Jobs.push_back({std::move(job), tag});
  }
  QueueCv.notify_one();
  return true;
}

void UJobThreadPool::WaitIdle()
{
  std::unique_lock<std::mutex> lock(QueueMutex);
  QueueCv.wait(lock, [this] { return Jobs.empty() && ActiveJobs == 0; });
}

bool UJobThreadPool::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  std::unique_lock<std::mutex> lock(QueueMutex);
  return QueueCv.wait_for(lock, timeout,
                          [this] { return Jobs.empty() && ActiveJobs == 0; });
}

void UJobThreadPool::CancelPendingJobs()
{
  std::lock_guard<std::mutex> lock(QueueMutex);
  Jobs.clear();
}

std::size_t UJobThreadPool::CancelPendingJobsIf(
    const std::function<bool(uint64_t)> &cancel)
{
  std::lock_guard<std::mutex> lock(QueueMutex);
  const auto before = Jobs.size();
  Jobs.erase(std::remove_if(Jobs.begin(), Jobs.end(),
                            [&](const QueuedJob &job) { return cancel(job.tag); }),
             Jobs.end());
  if (Jobs.empty() && ActiveJobs == 0) QueueCv.notify_all();
  return before - Jobs.size();
}

std::size_t UJobThreadPool::GetPendingJobCount() const
{
  std::lock_guard<std::mutex> lock(QueueMutex);
  return Jobs.size();
}

std::size_t UJobThreadPool::GetActiveJobCount() const
{
  std::lock_guard<std::mutex> lock(QueueMutex);
  return ActiveJobs;
}

void UJobThreadPool::WorkerLoop()
{
  CubatariumSetWorkerJobKind(WorkerJobKind.c_str());
  for (;;)
  {
    QueuedJob job;
    {
      std::unique_lock<std::mutex> lock(QueueMutex);
      QueueCv.wait(lock, [this] { return Stop || !Jobs.empty(); });
      if (Stop && Jobs.empty())
      {
        return;
      }
      job = std::move(Jobs.front());
      Jobs.pop_front();
      ++ActiveJobs;
    }
    job.run();
    // Idle includes release of closure-owned snapshot/work-slot credits.
    job.run = {};
    {
      std::lock_guard<std::mutex> lock(QueueMutex);
      --ActiveJobs;
      if (Jobs.empty() && ActiveJobs == 0)
      {
        QueueCv.notify_all();
      }
    }
  }
}

} // namespace cutum
