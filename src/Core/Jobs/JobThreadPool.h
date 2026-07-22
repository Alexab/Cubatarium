#pragma once

#include <algorithm>
#include <atomic>
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
  /// Stop workers for process exit: wait up to timeout, then detach leftovers
  /// so destructors never block forever on late ChunkPopulate/carve.
  void ShutdownForProcessExit(std::chrono::milliseconds timeout);
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
  void SetCapacity(std::size_t cap)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    Cap = cap;
    if (Cap > 0)
    {
      Items.reserve(Cap);
    }
  }

  std::size_t Capacity() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Cap;
  }

  uint64_t DiscardedOverflow() const
  {
    return Discarded.load(std::memory_order_relaxed);
  }

  void Push(T value)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    Items.push_back(std::move(value));
  }

  /// Push with drop-oldest when Cap > 0 and full. Returns true if an item was
  /// discarded (copied into dropped_out when non-null).
  bool PushDropOldest(T &&item, T *dropped_out = nullptr)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (Cap > 0 && Items.size() >= Cap)
    {
      if (dropped_out)
      {
        *dropped_out = std::move(Items.front());
      }
      Items.erase(Items.begin());
      Discarded.fetch_add(1, std::memory_order_relaxed);
      Items.push_back(std::move(item));
      return true;
    }
    Items.push_back(std::move(item));
    return false;
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

  std::size_t Size() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Items.size();
  }

private:
  mutable std::mutex Mutex;
  std::vector<T> Items;
  std::size_t Cap{0};
  std::atomic<uint64_t> Discarded{0};
};

} // namespace cutum
