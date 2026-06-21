#include "Core/Jobs/JobThreadPool.h"
#include <algorithm>
#include <thread>

namespace cutum
{

UJobThreadPool::UJobThreadPool(std::size_t threadCount)
{
  if (threadCount == 0)
  {
    const std::size_t hw = std::thread::hardware_concurrency();
    threadCount = hw > 1 ? hw - 1 : 1;
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
  for (;;)
  {
    std::unique_lock<std::mutex> lock(QueueMutex);
    if (Jobs.empty() && ActiveJobs == 0)
    {
      return;
    }
    lock.unlock();
    std::this_thread::yield();
  }
}

void UJobThreadPool::WorkerLoop()
{
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
