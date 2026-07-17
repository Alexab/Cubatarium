#include "Core/Jobs/JobThreadPool.h"
#include "App/Platform/Log.h"
#include "Core/Jobs/JobThreadBudget.h"
#include <thread>

namespace cutum
{

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
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    Jobs.push_back(std::move(job));
  }
  QueueCv.notify_one();
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
    std::function<void()> job;
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
    job();
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
