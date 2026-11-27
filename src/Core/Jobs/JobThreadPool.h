#pragma once

#include "Core/Memory/AllocationCountingResource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
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
  struct WorkerStorageSnapshot { size_t workers{0},thread_slots{0},thread_bytes{0}; };
  struct StackStorageSnapshot { bool observed{false}; size_t reservation_bytes{0},committed_bytes{0}; };
  // Calling thread's native stack allocation. Committed bytes are a point
  // sample, not maximum stack usage or a process-memory measurement.
  static StackStorageSnapshot GetCurrentThreadStackStorage();
  // Ordinary C++ request carried by one MSVC std::thread until its entry
  // returns. Zero means the launch implementation has no supported profile.
  static size_t GetWorkerLaunchRequestBytes() noexcept;
  WorkerStorageSnapshot GetWorkerStorageSnapshot() const
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    return {Workers.size(),Workers.capacity(),Workers.capacity()*sizeof(std::thread)};
  }
  explicit UJobThreadPool(std::size_t threadCount = 0,
                          const char *worker_job_kind = "Worker");
  ~UJobThreadPool();

  UJobThreadPool(const UJobThreadPool &) = delete;
  UJobThreadPool &operator=(const UJobThreadPool &) = delete;

  /// Lossless legacy submission; no queue cap. New bounded producers must
  /// handle TryEnqueue(false) without discarding their demand.
  void Enqueue(std::function<void()> job);
  // Admission is owned by the caller. Publish a prepared batch together.
  void EnqueueBatch(std::vector<std::function<void()>> jobs);
  /// Returns false if the pending queue is at MaxPendingJobs (job not queued).
  bool TryEnqueue(std::function<void()> job, uint64_t tag = 0);
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPendingJobs();
  /// Cancel selected queued jobs; running closures retain their normal owner.
  std::size_t CancelPendingJobsIf(const std::function<bool(uint64_t)> &cancel);
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
  // The visitor may read queued closures, which cannot move until it returns.
  // It must not submit work or otherwise reenter this pool.
  template<class Visit, class Finish>
  void InspectPendingJobs(Visit &&visit, Finish &&finish) const
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    for (const auto &job : Jobs)
      visit(job.run);
    finish(JobThreadPoolSnapshot{Jobs.size(), ActiveJobs, Workers.size()},
           QueueMemory.GetSnapshot().live_bytes);
  }
  // Queue node/map allocations only. Serialized with all deque mutations;
  // callable payloads, workers and upstream allocator overhead are separate.
  UAllocationCountingResource::Snapshot GetQueueAllocationSnapshot() const
  {
    std::lock_guard<std::mutex> lock(QueueMutex);
    return QueueMemory.GetSnapshot();
  }
  std::size_t GetWorkerCount() const { return Workers.size(); }
  std::size_t GetMaxPendingJobCount() const { return MaxPendingJobs; }
  uint64_t GetRejectedEnqueueCount() const { return RejectedEnqueues.load(); }
  void SetMaxPendingJobCount(std::size_t cap)
  {
    MaxPendingJobs = cap > 0 ? cap : kDefaultMaxPendingJobsPerPool;
  }

private:
  struct WorkerEntry
  {
    UJobThreadPool *owner;
    void operator()() const { owner->WorkerLoop(); }
  };
  void WorkerLoop();

  std::vector<std::thread> Workers;
  mutable std::mutex QueueMutex;
  std::condition_variable QueueCv;
  struct QueuedJob
  {
    std::function<void()> run;
    uint64_t tag{0};
  };
  UAllocationCountingResource QueueMemory;
  std::pmr::deque<QueuedJob> Jobs{&QueueMemory};
  std::size_t ActiveJobs{0};
  std::size_t MaxPendingJobs{kDefaultMaxPendingJobsPerPool};
  std::atomic<uint64_t> RejectedEnqueues{0};
  bool Stop{false};
  std::string WorkerJobKind;
};

template <typename T> class UCompletedJobQueue
{
public:
  // Synchronous const inspection of actual retained values. Does not copy or
  // drain owners; callback runs under this queue's mutex and must not reenter.
  template<class Visit> void InspectValues(Visit &&visit) const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    for(size_t i=0;i<Count;++i)visit(Items[(Head+i)%Items.size()]);
  }
  template<class Capacity, class Visit>
  void InspectStorage(Capacity &&capacity, Visit &&visit) const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    capacity(Items.capacity());
    for (size_t i = 0; i < Count; ++i)
      visit(Items[(Head + i) % Items.size()]);
  }
  struct StorageSnapshot { size_t count{0},slots{0},bytes{0}; };
  StorageSnapshot GetStorageSnapshot() const
  {
    std::lock_guard<std::mutex> lock(Mutex);
    return {Count,Items.capacity(),Items.capacity()*sizeof(T)};
  }
  // Preallocate slots without imposing a drop policy. Producers still own
  // admission and may use the existing lossless queue behavior.
  void ReserveStorage(size_t slots)
  {
    std::lock_guard<std::mutex> lock(Mutex);
    Items.reserve(slots);
  }
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

  void Push(T value, double *mutex_wait_ms = nullptr,
            double *mutex_held_ms = nullptr)
  {
    if (!mutex_wait_ms && !mutex_held_ms)
    {
      std::lock_guard<std::mutex> lock(Mutex);
      PushUnlocked(std::move(value), nullptr);
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
    PushUnlocked(std::move(value), nullptr);
    if (mutex_held_ms)
    {
      *mutex_held_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - lock_acquired)
                           .count();
    }
  }

  /// Requeue a batch while acquiring the queue lock only once.
  void PushRange(std::vector<T> &&values, double *mutex_wait_ms = nullptr,
                 double *mutex_held_ms = nullptr)
  {
    PushRange(values.data(), values.size(), mutex_wait_ms, mutex_held_ms);
  }

  void PushRange(T *values, std::size_t count, double *mutex_wait_ms = nullptr,
                 double *mutex_held_ms = nullptr)
  {
    if (count == 0)
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
    for (std::size_t i = 0; i < count; ++i)
    {
      PushUnlocked(std::move(values[i]), nullptr);
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

  std::vector<T> DrainAll(double *mutex_wait_ms = nullptr,
                          double *mutex_held_ms = nullptr)
  {
    auto drain_unlocked = [&]()
    {
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
    };
    if (!mutex_wait_ms && !mutex_held_ms)
    {
      std::lock_guard<std::mutex> lock(Mutex);
      return drain_unlocked();
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
    std::vector<T> drained = drain_unlocked();
    if (mutex_held_ms)
    {
      *mutex_held_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - lock_acquired)
                           .count();
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

  /// Compute one rank key per item. An optional consumer gets the first
  /// opportunity; the rest of the batch keeps the ordinary ranking.
  template <typename KeyFn, typename Required = std::nullptr_t>
  std::vector<T> DrainBestByKeyUpTo(std::size_t maxCount, KeyFn &&key_fn,
                                    std::size_t *availableCount = nullptr,
                                    double *mutex_wait_ms = nullptr,
                                    double *mutex_held_ms = nullptr,
                                    Required required = nullptr)
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
    std::optional<RankedOffset> preferred;
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
      const T &item = item_at(offset);
      RankedOffset candidate{key_fn(item), offset};
      if constexpr (!std::is_same_v<Required, std::nullptr_t>)
      {
        if (required(item) && (!preferred || ranked_before(candidate, *preferred)))
          preferred.emplace(candidate);
      }
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
    if constexpr (!std::is_same_v<Required, std::nullptr_t>)
    {
      if (preferred)
      {
        const auto match = std::find_if(ranked.begin(), ranked.end(), [&](const auto &choice) {
          return choice.offset == preferred->offset;
        });
        if (match != ranked.end())
          std::rotate(ranked.begin(), match, std::next(match));
        else
        {
          std::move_backward(ranked.begin(), std::prev(ranked.end()), ranked.end());
          ranked.front() = std::move(*preferred);
        }
      }
    }

    // All backing must be available before moving any queue entry.
    drained.reserve(take);

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

    const auto item_at = [this](std::size_t offset) -> T &
    { return Items[(Head + offset) % Items.size()]; };
    std::size_t removed = 0;
    for (std::size_t i = 0; i < Count; ++i)
    {
      T &item = item_at(i);
      if (pred(item))
      {
        // Retire the entire value, including owners in its moved-from slot.
        std::exchange(item, T{});
        ++removed;
      }
      else if (removed)
        item_at(i - removed) = std::move(item);
    }

    for (std::size_t i = Count - removed; i < Count; ++i)
      item_at(i) = T{};
    Count -= removed;
    if (Count == 0)
    {
      Head = 0;
      if (Cap == 0)
        Items.clear();
    }
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
    if (Head != 0 && Items.size() < Items.capacity())
    {
      // A reserved ring can already fit this append. Preserve its allocation
      // and logical ordering when the occupied range wraps.
      std::rotate(Items.begin(), Items.begin() + Head, Items.end());
      Head = 0;
    }
    else if (Head != 0)
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
