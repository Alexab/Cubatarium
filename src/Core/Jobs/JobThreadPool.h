#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cutum
{

/// Default cap on queued (not yet running) jobs per pool. Worker count is unified
/// via ComputeWorkerThreadCount(JobPoolKind, override); queue depth is separate.
constexpr std::size_t kDefaultMaxPendingJobsPerPool = 256;

struct JobThreadPoolSnapshot
{
  std::size_t pending{0};
  std::size_t active{0};
  std::size_t workers{0};
};

class UJobThreadPool
{
public:
  explicit UJobThreadPool(std::size_t threadCount = 0,
                          const char *worker_job_kind = "Worker");
  ~UJobThreadPool();

  UJobThreadPool(const UJobThreadPool &) = delete;
  UJobThreadPool &operator=(const UJobThreadPool &) = delete;

  /// Lossless legacy submission; no queue cap. New bounded producers must
  /// handle TryEnqueue(false) without discarding their demand.
  void Enqueue(std::function<void()> job);
  /// Returns false if the pending queue is at MaxPendingJobs (job not queued).
  bool TryEnqueue(std::function<void()> job);
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPendingJobs();
  /// Stop workers for process exit: wait up to timeout, then detach leftovers
  /// so destructors never block forever on late ChunkPopulate/carve.
  void ShutdownForProcessExit(std::chrono::milliseconds timeout);
  std::size_t GetPendingJobCount() const;
  std::size_t GetActiveJobCount() const;
  JobThreadPoolSnapshot GetSnapshot() const
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    return JobThreadPoolSnapshot{Jobs.size(), ActiveJobs, Workers.size()};
  }
  std::size_t GetWorkerCount() const { return Workers.size(); }
  std::size_t GetMaxPendingJobCount() const { return MaxPendingJobs; }
  uint64_t GetRejectedEnqueueCount() const { return RejectedEnqueues.load(); }
  void SetMaxPendingJobCount(std::size_t cap)
  {
    MaxPendingJobs = cap > 0 ? cap : kDefaultMaxPendingJobsPerPool;
  }

private:
  void WorkerLoop();

  std::vector<std::thread> Workers;
  mutable std::mutex QueueMutex;
  std::condition_variable QueueCv;
  std::deque<std::function<void()>> Jobs;
  std::size_t ActiveJobs{0};
  std::size_t MaxPendingJobs{kDefaultMaxPendingJobsPerPool};
  std::atomic<uint64_t> RejectedEnqueues{0};
  bool Stop{false};
  std::string WorkerJobKind;
};

template <typename T> class UCompletedJobQueue
{
public:
  // Return every evicted result so its owner can retire its token and retry.
  std::vector<T> SetCapacity(std::size_t cap)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (cap == Cap)
    {
      return {};
    }
    // Drain to linear vector, then rebuild ring at new capacity.
    std::vector<T> kept;
    std::vector<T> dropped;
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
      dropped.reserve(drop_n);
      for (std::size_t i = 0; i < drop_n; ++i)
        dropped.push_back(std::move(kept[i]));
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
    return dropped;
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

  /// Requeue a batch while acquiring the queue lock only once.
  void PushRange(std::vector<T> &&values, double *mutex_wait_ms = nullptr,
                 double *mutex_held_ms = nullptr)
  {
    if (values.empty())
    {
      if (mutex_wait_ms)
      {
        *mutex_wait_ms = 0.0;
      }
      if (mutex_held_ms)
      {
        *mutex_held_ms = 0.0;
      }
      return;
    }
    const auto lock_wait_started = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(Mutex);
    const auto lock_acquired = std::chrono::steady_clock::now();
    if (mutex_wait_ms)
    {
      *mutex_wait_ms = std::chrono::duration<double, std::milli>(
                           lock_acquired - lock_wait_started)
                           .count();
    }
    for (T &value : values)
    {
      PushUnlocked(std::move(value), nullptr);
    }
    if (mutex_held_ms)
    {
      *mutex_held_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - lock_acquired)
                           .count();
    }
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

  /// Drain the best available entries without sorting or moving the rest of
  /// the queue. Useful for bounded consumers whose priorities can change as
  /// the focus moves (for example, disk results near the current camera).
  template <typename Compare>
  std::vector<T> DrainBestUpTo(std::size_t maxCount, Compare &&compare)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    std::vector<T> drained;
    if (maxCount == 0 || Count == 0 || Items.empty())
    {
      return drained;
    }

    const std::size_t take = std::min(maxCount, Count);
    const auto item_at = [this](std::size_t offset) -> T &
    { return Items[(Head + offset) % Items.size()]; };
    // Selection sort only the bounded prefix. The completion queue can grow
    // during long flights, while callers normally drain only a few results.
    for (std::size_t selected = 0; selected < take; ++selected)
    {
      std::size_t best = selected;
      for (std::size_t candidate = selected + 1; candidate < Count;
           ++candidate)
      {
        if (compare(item_at(candidate), item_at(best)))
        {
          best = candidate;
        }
      }
      if (best != selected)
      {
        using std::swap;
        swap(item_at(selected), item_at(best));
      }
    }

    drained.reserve(take);
    for (std::size_t i = 0; i < take; ++i)
    {
      drained.push_back(std::move(item_at(i)));
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

  /// Drain a bounded best-first batch after computing one rank key per item.
  /// This avoids recalculating distance/age state for every selection pass.
  template <typename KeyFn>
  std::vector<T> DrainBestByKeyUpTo(std::size_t maxCount, KeyFn &&key_fn,
                                    std::size_t *availableCount = nullptr,
                                    double *mutex_wait_ms = nullptr,
                                    double *mutex_held_ms = nullptr)
  {
    using Key = std::decay_t<decltype(key_fn(std::declval<const T &>()))>;
    struct RankedOffset
    {
      Key key;
      std::size_t offset{0};
    };

    const auto lock_wait_started = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(Mutex);
    const auto lock_acquired = std::chrono::steady_clock::now();
    if (mutex_wait_ms)
    {
      *mutex_wait_ms = std::chrono::duration<double, std::milli>(
                           lock_acquired - lock_wait_started)
                           .count();
    }
    std::vector<T> drained;
    if (availableCount)
    {
      *availableCount = Count;
    }
    if (maxCount == 0 || Count == 0 || Items.empty())
    {
      if (mutex_held_ms)
      {
        *mutex_held_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - lock_acquired)
                             .count();
      }
      return drained;
    }

    const std::size_t take = std::min(maxCount, Count);
    const auto item_at = [this](std::size_t offset) -> T &
    { return Items[(Head + offset) % Items.size()]; };
    std::vector<RankedOffset> ranked;
    const auto ranked_before = [](const RankedOffset &a,
                                  const RankedOffset &b)
    {
      if (a.key < b.key)
      {
        return true;
      }
      if (b.key < a.key)
      {
        return false;
      }
      // Preserve queue order when the caller's rank keys tie.
      return a.offset < b.offset;
    };
    ranked.reserve(take);
    for (std::size_t offset = 0; offset < Count; ++offset)
    {
      RankedOffset candidate{key_fn(item_at(offset)), offset};
      if (ranked.size() < take)
      {
        ranked.push_back(std::move(candidate));
        std::push_heap(ranked.begin(), ranked.end(), ranked_before);
      }
      else if (ranked_before(candidate, ranked.front()))
      {
        std::pop_heap(ranked.begin(), ranked.end(), ranked_before);
        ranked.back() = std::move(candidate);
        std::push_heap(ranked.begin(), ranked.end(), ranked_before);
      }
    }
    std::sort(ranked.begin(), ranked.end(), ranked_before);

    // Move selected items into a best-first prefix using at most `take`
    // swaps. Track only selected offsets; avoid two scratch arrays sized to
    // the entire ready queue.
    for (std::size_t selected = 0; selected < take; ++selected)
    {
      const std::size_t desired_position = ranked[selected].offset;
      if (desired_position == selected)
      {
        continue;
      }
      using std::swap;
      swap(item_at(selected), item_at(desired_position));
      for (std::size_t pending = selected + 1; pending < take; ++pending)
      {
        if (ranked[pending].offset == selected)
        {
          ranked[pending].offset = desired_position;
        }
      }
    }

    drained.reserve(take);
    for (std::size_t i = 0; i < take; ++i)
    {
      drained.push_back(std::move(item_at(i)));
    }
    Head = (Head + take) % Items.size();
    Count -= take;
    if (Cap == 0 && Count == 0)
    {
      Items.clear();
      Head = 0;
    }
    if (mutex_held_ms)
    {
      *mutex_held_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - lock_acquired)
                           .count();
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

  /// Remove queued results which no longer have a live owner. Preserves the
  /// relative order of retained entries and releases storage for erased ones.
  template <typename Pred> std::size_t EraseIf(Pred &&pred)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (Count == 0 || Items.empty())
    {
      return 0;
    }

    std::vector<T> kept;
    kept.reserve(Count);
    std::size_t removed = 0;
    const std::size_t old_count = Count;
    for (std::size_t i = 0; i < old_count; ++i)
    {
      T &item = Items[(Head + i) % Items.size()];
      if (pred(item))
      {
        ++removed;
      }
      else
      {
        kept.push_back(std::move(item));
      }
    }
    if (removed == 0)
    {
      for (std::size_t i = 0; i < kept.size(); ++i)
      {
        Items[(Head + i) % Items.size()] = std::move(kept[i]);
      }
      return 0;
    }

    if (Cap == 0)
    {
      Items = std::move(kept);
      Head = 0;
      Count = Items.size();
      return removed;
    }

    const std::size_t kept_count = kept.size();
    for (std::size_t i = 0; i < kept_count; ++i)
    {
      Items[i] = std::move(kept[i]);
    }
    for (std::size_t i = kept_count; i < old_count; ++i)
    {
      Items[i] = T{};
    }
    Head = 0;
    Count = kept_count;
    return removed;
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
    // Unbounded queues keep ring storage after a partial drain. Reuse those
    // free slots before compacting the active range.
    if (Count < Items.size())
    {
      const std::size_t slot = (Head + Count) % Items.size();
      Items[slot] = std::move(item);
      ++Count;
      return false;
    }
    // Grow a full wrapped ring from its logical Head==0 order.
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
