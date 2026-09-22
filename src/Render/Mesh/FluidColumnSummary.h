#ifndef FLUID_COLUMN_SUMMARY_H
#define FLUID_COLUMN_SUMMARY_H

#include "World/Chunks/Chunk.h"

#include <cstdint>
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

/// A26 N5: enqueue worker rebuild when sync scan deferred (stub API).
struct FluidSummaryWorkerJob
{
  FluidColumnSummaryRequest req{};
  bool enqueued{false};
};

inline bool TryEnqueueFluidSummaryWorker(FluidSummaryWorkerJob &job,
                                         const FluidColumnSummaryRequest &req,
                                         bool defer_sync_scan)
{
  if (!defer_sync_scan)
  {
    return false;
  }
  job.req = req;
  job.enqueued = true;
  return true;
}

/// A26 N5: hitch gate — reject sync fluid_map work that would burn tens/hundreds ms.
inline bool ShouldRejectFluidMapHitch(double estimated_ms, double budget_ms = 8.0)
{
  return estimated_ms > budget_ms;
}

} // namespace cutum

#endif
