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
    if (cap == Cap)
    {
      return;
    }
    // Drain to linear vector, then rebuild ring at new capacity.
    std::vector<T> kept;
    kept.reserve(Count);
    for (std::size_t i = 0; i < Count; ++i)
    {
      kept.push_back(std::move(Items[(Head + i) % Items.size()]));
    }
    Cap = cap;
    Head = 0;
    Count = 0;
    Items.clear();
    if (Cap > 0)
    {
      Items.resize(Cap);
      const std::size_t keep_n =
          (kept.size() > Cap) ? Cap : kept.size();
      const std::size_t drop_n = kept.size() - keep_n;
      // Keep newest keep_n entries when shrinking.
      for (std::size_t i = drop_n; i < kept.size(); ++i)
      {
        Items[Count++] = std::move(kept[i]);
      }
      if (drop_n > 0)
      {
        Discarded.fetch_add(drop_n, std::memory_order_relaxed);
      }
    }
    else
    {
      Items = std::move(kept);
      Count = Items.size();
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
    PushUnlocked(std::move(value), nullptr);
  }

  /// Push with drop-oldest when Cap > 0 and full. Returns true if an item was
  /// discarded (moved into dropped_out when non-null).
  bool PushDropOldest(T &&item, T *dropped_out = nullptr)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return PushUnlocked(std::move(item), dropped_out);
  }

  std::vector<T> DrainAll()
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    drained.reserve(Count);
    for (std::size_t i = 0; i < Count; ++i)
    {
      drained.push_back(std::move(Items[(Head + i) % Items.size()]));
    }
    Head = 0;
    Count = 0;
    if (Cap == 0)
    {
      Items.clear();
    }
    return drained;
  }

  std::vector<T> DrainUpTo(std::size_t maxCount)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    if (maxCount == 0 || Count == 0)
    {
      return drained;
    }
    const std::size_t take = std::min(maxCount, Count);
    drained.reserve(take);
    for (std::size_t i = 0; i < take; ++i)
    {
      drained.push_back(std::move(Items[(Head + i) % Items.size()]));
    }
    Head = (Head + take) % Items.size();
    Count -= take;
    if (Cap == 0 && Count == 0)
    {
      Items.clear();
      Head = 0;
    }
    return drained;
  }

  bool Empty() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Count == 0;
  }

  std::size_t Size() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return Count;
  }

  /// Peek without drain — e.g. near-radius enter ring vs far Completed.
  template <typename Pred> bool Any(Pred &&pred) const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (Count == 0 || Items.empty())
    {
      return false;
    }
    for (std::size_t i = 0; i < Count; ++i)
    {
      if (pred(Items[(Head + i) % Items.size()]))
      {
        return true;
      }
    }
    return false;
  }

private:
  bool PushUnlocked(T &&item, T *dropped_out)
  {
    if (Cap > 0)
    {
      if (Items.size() != Cap)
      {
        Items.resize(Cap);
        Head = 0;
        Count = 0;
      }
      if (Count >= Cap)
      {
        if (dropped_out)
        {
          *dropped_out = std::move(Items[Head]);
        }
        Head = (Head + 1) % Cap;
        --Count;
        Discarded.fetch_add(1, std::memory_order_relaxed);
        const std::size_t slot = (Head + Count) % Cap;
        Items[slot] = std::move(item);
        ++Count;
        return true;
      }
      const std::size_t slot = (Head + Count) % Cap;
      Items[slot] = std::move(item);
      ++Count;
      return false;
    }
    // Unbounded (Cap==0): grow vector from Head==0 layout.
    if (Head != 0)
    {
      std::vector<T> linear;
      linear.reserve(Count + 1);
      for (std::size_t i = 0; i < Count; ++i)
      {
        linear.push_back(std::move(Items[(Head + i) % Items.size()]));
      }
      Items = std::move(linear);
      Head = 0;
    }
    Items.push_back(std::move(item));
    Count = Items.size();
    return false;
  }

  mutable std::mutex Mutex;
  std::vector<T> Items;
  std::size_t Cap{0};
  std::size_t Head{0};
  std::size_t Count{0};
  std::atomic<uint64_t> Discarded{0};
};

} // namespace cutum
