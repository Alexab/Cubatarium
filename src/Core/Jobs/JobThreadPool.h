#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cutum
{

class UJobThreadPool
{
public:
  explicit UJobThreadPool(std::size_t threadCount = 0,
                          const char *worker_job_kind = "Worker");
  ~UJobThreadPool();

  UJobThreadPool(const UJobThreadPool &) = delete;
  UJobThreadPool &operator=(const UJobThreadPool &) = delete;

  void Enqueue(std::function<void()> job);
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPendingJobs();
  std::size_t GetPendingJobCount() const;
  std::size_t GetActiveJobCount() const;

private:
  void WorkerLoop();

  std::vector<std::thread> Workers;
  mutable std::mutex QueueMutex;
  std::condition_variable QueueCv;
  std::deque<std::function<void()>> Jobs;
  std::size_t ActiveJobs{0};
  bool Stop{false};
  std::string WorkerJobKind;
};

template <typename T> class UCompletedJobQueue
{
public:
  void Push(T value)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    Items.push_back(std::move(value));
  }

  std::vector<T> DrainAll()
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    drained.swap(Items);
    return drained;
  }

  std::vector<T> DrainUpTo(std::size_t maxCount)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    if (maxCount == 0 || Items.empty())
    {
      return drained;
    }
    const std::size_t take = std::min(maxCount, Items.size());
    drained.reserve(take);
    for (std::size_t i = 0; i < take; ++i)
    {
      drained.push_back(std::move(Items[i]));
    }
    Items.erase(Items.begin(),
                Items.begin() + static_cast<std::ptrdiff_t>(take));
    return drained;
  }

  bool Empty() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Items.empty();
  }

private:
  mutable std::mutex Mutex;
  std::vector<T> Items;
};

} // namespace cutum
