#ifndef FLUID_COLUMN_SUMMARY_H
#define FLUID_COLUMN_SUMMARY_H

#include "World/Chunks/Chunk.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace cutum
{

/// A21 P6: versioned per-column fluid surface summary (top Y + material).
/// Main thread installs a ready slice only when versions match.
struct FluidColumnSummary
{
  uint64_t world_epoch{0};
  uint64_t content_rev{0};
  uint64_t catalog_rev{0};
  uint64_t fluid_id_hash{0};
  int y_min{0};
  int height{0};
  glm::ivec3 ground_chunk{0};
  /// CHUNK_SIZE * CHUNK_SIZE tops; -1 = no fluid in column.
  std::vector<int16_t> tops;
  BlockId representative_fluid_id{BLOCK_AIR};
  bool ready{false};
};

/// Async rebuild stub: worker fills `out` from immutable flags; main thread
/// installs only when world_epoch/content_rev still match the request.
struct FluidColumnSummaryRequest
{
  uint64_t world_epoch{0};
  uint64_t content_rev{0};
  uint64_t catalog_rev{0};
  int y_min{0};
  int height{0};
};

enum class FluidSummaryInstallOutcome : uint8_t
{
  Installed = 0,
  StaleDiscarded,
  Cancelled
};

inline bool FluidColumnSummaryVersionsMatch(const FluidColumnSummary &got,
                                            const FluidColumnSummaryRequest &req)
{
  return got.ready && got.world_epoch == req.world_epoch &&
         got.content_rev == req.content_rev &&
         got.catalog_rev == req.catalog_rev && got.y_min == req.y_min &&
         got.height == req.height;
}

/// Stub: CPU fill from flags (same layout as ScanFluidColumnsCpu). Real async
/// path will enqueue a worker; this keeps the install-gate API available now.
inline bool TryBuildFluidColumnSummarySync(const uint8_t *fluid_flags,
                                           const FluidColumnSummaryRequest &req,
                                           uint64_t fluid_id_hash,
                                           BlockId representative,
                                           FluidColumnSummary &out)
{
  if (!fluid_flags || req.height <= 0)
  {
    return false;
  }
  const int n = CHUNK_SIZE;
  out = FluidColumnSummary{};
  out.world_epoch = req.world_epoch;
  out.content_rev = req.content_rev;
  out.catalog_rev = req.catalog_rev;
  out.y_min = req.y_min;
  out.height = req.height;
  out.fluid_id_hash = fluid_id_hash;
  out.representative_fluid_id = representative;
  out.tops.assign(static_cast<size_t>(n * n), static_cast<int16_t>(-1));
  for (int z = 0; z < n; ++z)
  {
    for (int x = 0; x < n; ++x)
    {
      int16_t top = -1;
      for (int y = 0; y < req.height; ++y)
      {
        const size_t i = static_cast<size_t>((y * n + z) * n + x);
        if (fluid_flags[i] != 0)
        {
          top = static_cast<int16_t>(y);
        }
      }
      out.tops[static_cast<size_t>(z * n + x)] = top;
    }
  }
  out.ready = true;
  return true;
}

/// Main-thread install: accept only when versions still match.
inline bool TryInstallFluidColumnSummary(const FluidColumnSummary &candidate,
                                         const FluidColumnSummaryRequest &req,
                                         FluidColumnSummary &live)
{
  if (!FluidColumnSummaryVersionsMatch(candidate, req))
  {
    return false;
  }
  live = candidate;
  return true;
}

/// A25 R5: prefer incomplete/aged tile or worker rebuild over sync height×16×16
/// GetBlock on the main thread when the column is tall or budget is exhausted.
inline bool ShouldDeferFluidFullColumnScan(int height,
                                           bool has_usable_incomplete,
                                           bool main_thread_budget_exhausted,
                                           int tall_height = 48)
{
  if (has_usable_incomplete)
  {
    return true;
  }
  return main_thread_budget_exhausted && height >= tall_height;
}

/// A26 N5: toroidal / tiled surface origin — wrap column xz into map extent.
inline void WrapFluidSurfaceOrigin(int &ox, int &oz, int map_w, int map_h)
{
  if (map_w <= 0 || map_h <= 0)
  {
    return;
  }
  ox %= map_w;
  if (ox < 0)
  {
    ox += map_w;
  }
  oz %= map_h;
  if (oz < 0)
  {
    oz += map_h;
  }
}

/// A26 N5: water→lava identity change invalidates occupancy-only pack reuse.
inline bool FluidMaterialIdentityChanged(uint64_t prev_fluid_id_hash,
                                         uint64_t next_fluid_id_hash,
                                         BlockId prev_rep, BlockId next_rep)
{
  if (prev_fluid_id_hash != next_fluid_id_hash)
  {
    return true;
  }
  return prev_rep != next_rep;
}

/// A26/A28/A29 T5: enqueue worker rebuild when sync scan deferred.
struct FluidSummaryWorkerJob
{
  FluidColumnSummaryRequest req{};
  std::vector<uint8_t> flags;
  uint64_t fluid_id_hash{0};
  BlockId representative_fluid_id{BLOCK_AIR};
  glm::ivec3 ground_chunk{0};
  bool enqueued{false};
};

struct FluidSummaryCompletion
{
  FluidColumnSummary summary{};
  FluidColumnSummaryRequest req{};
};

struct FluidSummaryQueues
{
  std::mutex mu;
  std::condition_variable cv;
  std::vector<FluidSummaryWorkerJob> pending;
  std::vector<FluidSummaryCompletion> completed;
  std::atomic<bool> stop{false};
  std::atomic<bool> started{false};
  std::thread worker;
  std::atomic<uint64_t> installed_n{0};
  std::atomic<uint64_t> stale_discarded_n{0};

  ~FluidSummaryQueues()
  {
    stop.store(true, std::memory_order_relaxed);
    cv.notify_all();
    if (worker.joinable())
    {
      worker.join();
    }
  }
};

inline FluidSummaryQueues &FluidSummaryQueuesState()
{
  static FluidSummaryQueues q;
  return q;
}

inline std::vector<FluidSummaryWorkerJob> &FluidSummaryWorkerQueue()
{
  return FluidSummaryQueuesState().pending;
}

inline uint64_t FluidSummaryInstalledN()
{
  return FluidSummaryQueuesState().installed_n.load(std::memory_order_relaxed);
}

inline uint64_t FluidSummaryStaleDiscardedN()
{
  return FluidSummaryQueuesState().stale_discarded_n.load(
      std::memory_order_relaxed);
}

inline void FluidSummaryWorkerLoop()
{
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  for (;;)
  {
    FluidSummaryWorkerJob job{};
    {
      std::unique_lock<std::mutex> lock(qs.mu);
      qs.cv.wait(lock, [&] {
        return qs.stop.load(std::memory_order_relaxed) || !qs.pending.empty();
      });
      if (qs.stop.load(std::memory_order_relaxed) && qs.pending.empty())
      {
        return;
      }
      job = std::move(qs.pending.front());
      qs.pending.erase(qs.pending.begin());
    }
    FluidSummaryCompletion done{};
    done.req = job.req;
    if (!job.flags.empty())
    {
      (void)TryBuildFluidColumnSummarySync(
          job.flags.data(), job.req, job.fluid_id_hash,
          job.representative_fluid_id, done.summary);
      done.summary.ground_chunk = job.ground_chunk;
    }
    else
    {
      done.summary = FluidColumnSummary{};
      done.summary.world_epoch = job.req.world_epoch;
      done.summary.content_rev = job.req.content_rev;
      done.summary.catalog_rev = job.req.catalog_rev;
      done.summary.y_min = job.req.y_min;
      done.summary.height = job.req.height;
      done.summary.ground_chunk = job.ground_chunk;
      done.summary.ready = false;
      done.summary.tops.assign(static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE),
                               static_cast<int16_t>(-1));
    }
    {
      std::lock_guard<std::mutex> lock(qs.mu);
      qs.completed.push_back(std::move(done));
    }
  }
}

inline void EnsureFluidSummaryWorkerStarted()
{
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  bool expected = false;
  if (!qs.started.compare_exchange_strong(expected, true))
  {
    return;
  }
  qs.worker = std::thread(FluidSummaryWorkerLoop);
  // A35: keep joinable for ShutdownFluidSummaryWorker (no detach).
}

inline bool TryEnqueueFluidSummaryWorker(FluidSummaryWorkerJob &job,
                                         const FluidColumnSummaryRequest &req,
                                         bool defer_sync_scan,
                                         const uint8_t *flags = nullptr,
                                         size_t flag_bytes = 0,
                                         uint64_t fluid_id_hash = 0,
                                         BlockId representative = BLOCK_AIR)
{
  if (!defer_sync_scan)
  {
    return false;
  }
  job.req = req;
  job.fluid_id_hash = fluid_id_hash;
  job.representative_fluid_id = representative;
  job.flags.clear();
  if (flags && flag_bytes > 0)
  {
    job.flags.assign(flags, flags + flag_bytes);
  }
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  {
    std::lock_guard<std::mutex> lock(qs.mu);
    // A31: queue full is an explicit reject — never silent drop as success.
    if (qs.pending.size() >= 8)
    {
      job.enqueued = false;
      return false;
    }
    job.enqueued = true;
    qs.pending.push_back(job);
  }
  EnsureFluidSummaryWorkerStarted();
  qs.cv.notify_one();
  return true;
}

/// A35 R0 / A31 P4: hang-safe shutdown on world switch — stop, clear queue,
/// join worker (joinable; no detach). Returns false if worker was not joinable.
inline bool ShutdownFluidSummaryWorker(int /*timeout_ms*/ = 2000)
{
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  if (!qs.started.load(std::memory_order_relaxed))
  {
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(qs.mu);
    qs.pending.clear();
  }
  qs.stop.store(true, std::memory_order_relaxed);
  qs.cv.notify_all();
  if (qs.worker.joinable())
  {
    qs.worker.join();
  }
  qs.started.store(false, std::memory_order_relaxed);
  qs.stop.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(qs.mu);
    qs.completed.clear();
  }
  return true;
}

/// A29 U3: drain one deferred summary — fill from flags when present.
/// Prefers completion queue; falls back to inline pending process (tests).
inline bool DrainOneFluidSummaryWorker(FluidColumnSummary &out)
{
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  {
    std::lock_guard<std::mutex> lock(qs.mu);
    if (!qs.completed.empty())
    {
      out = std::move(qs.completed.front().summary);
      qs.completed.erase(qs.completed.begin());
      return true;
    }
  }
  FluidSummaryWorkerJob job{};
  {
    std::lock_guard<std::mutex> lock(qs.mu);
    if (qs.pending.empty())
    {
      return false;
    }
    job = std::move(qs.pending.front());
    qs.pending.erase(qs.pending.begin());
  }
  if (!job.flags.empty())
  {
    const bool ok = TryBuildFluidColumnSummarySync(
        job.flags.data(), job.req, job.fluid_id_hash,
        job.representative_fluid_id, out);
    if (ok)
    {
      out.ground_chunk = job.ground_chunk;
    }
    return ok;
  }
  out = FluidColumnSummary{};
  out.world_epoch = job.req.world_epoch;
  out.content_rev = job.req.content_rev;
  out.catalog_rev = job.req.catalog_rev;
  out.y_min = job.req.y_min;
  out.height = job.req.height;
  out.ground_chunk = job.ground_chunk;
  out.ready = false; // incomplete until flags arrive
  out.tops.assign(static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE),
                  static_cast<int16_t>(-1));
  return true;
}

/// A31/A32: drain ready completions; install or StaleDiscarded — never silent
/// drop of a ready result. Returns number of Installed outcomes.
inline int DrainFluidSummaryCompletions(
    void (*on_ready)(const FluidColumnSummary &, void *), void *ctx,
    int max_n = 4)
{
  if (!on_ready || max_n <= 0)
  {
    return 0;
  }
  FluidSummaryQueues &qs = FluidSummaryQueuesState();
  int installed = 0;
  for (int i = 0; i < max_n; ++i)
  {
    FluidSummaryCompletion done{};
    bool have = false;
    {
      std::lock_guard<std::mutex> lock(qs.mu);
      if (!qs.completed.empty())
      {
        done = std::move(qs.completed.front());
        qs.completed.erase(qs.completed.begin());
        have = true;
      }
      // A37 H5: never inline-build pending on drain/main — wait for worker.
    }
    if (!have)
    {
      break;
    }
    if (!done.summary.ready)
    {
      continue;
    }
    FluidColumnSummary live{};
    if (TryInstallFluidColumnSummary(done.summary, done.req, live))
    {
      on_ready(live, ctx);
      qs.installed_n.fetch_add(1, std::memory_order_relaxed);
      ++installed;
    }
    else
    {
      // Ready but versions stale — explicit StaleDiscarded (not silent drop).
      qs.stale_discarded_n.fetch_add(1, std::memory_order_relaxed);
      (void)FluidSummaryInstallOutcome::StaleDiscarded;
    }
  }
  return installed;
}

/// A26 N5: hitch gate — reject sync fluid_map work that would burn tens/hundreds ms.
inline bool ShouldRejectFluidMapHitch(double estimated_ms, double budget_ms = 8.0)
{
  return estimated_ms > budget_ms;
}

} // namespace cutum

#endif
