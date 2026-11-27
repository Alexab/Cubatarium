#include "Core/Jobs/JobThreadPool.h"
#include "App/Platform/Log.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Memory/NativeThreadStack.h"
#include <algorithm>
#include <thread>
#include <tuple>
namespace cutum
{

size_t UJobThreadPool::GetWorkerLaunchRequestBytes() noexcept
{
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  // The pinned STL _Start allocates tuple<decay_t<Callable>> via make_unique.
  return sizeof(std::tuple<WorkerEntry>);
#else
  return 0;
#endif
}

UJobThreadPool::StackStorageSnapshot UJobThreadPool::GetCurrentThreadStackStorage()
{
  const auto stack = ReadCurrentNativeStack();
  return {stack.observed, stack.reserved, stack.committed};
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
  try {
    for (std::size_t i = 0; i < threadCount; ++i)
      Workers.emplace_back(WorkerEntry{this});
  } catch(...) {
    // A partially constructed pool has no destructor. Retire already-started
    // workers before vector<thread> unwinds or referenced members disappear.
    {
      std::lock_guard<std::mutex> lock(QueueMutex);
      Stop=true;
    }
    QueueCv.notify_all();
    for(auto &worker:Workers)if(worker.joinable())worker.join();
    throw;
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

void UJobThreadPool::EnqueueBatch(std::vector<std::function<void()>> jobs)
{
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    const auto first = Jobs.size();
    try
    {
      for (auto &job : jobs)
        Jobs.push_back({std::move(job), 0});
    }
    catch (...)
    {
      while (Jobs.size() > first)
        Jobs.pop_back();
      throw;
    }
  }
  QueueCv.notify_all();
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
  const NativeThreadStackOwner nativeStack(NativeThreadKind::JobPool);
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
