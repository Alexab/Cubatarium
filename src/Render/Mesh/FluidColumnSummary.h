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

} // namespace cutum

#endif
