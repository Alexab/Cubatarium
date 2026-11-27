#pragma once

#include "World/Chunks/ChunkDimensions.h"
#include <algorithm>
#include <cstdint>

namespace cutum
{

// Inclusive offsets covering the affected terrain columns. Writes proven to
// stay in the source chunk are covered by the origin, including terrain.
struct WorldGenWriteExtent
{
  int64_t min_x{0}, max_x{0}, min_z{0}, max_z{0};

  void Include(const WorldGenWriteExtent &other)
  {
    min_x = std::min(min_x, other.min_x);
    max_x = std::max(max_x, other.max_x);
    min_z = std::min(min_z, other.min_z);
    max_z = std::max(max_z, other.max_z);
  }

  void IncludeRadius(int64_t x, int64_t z)
  {
    Include({-x, x, -z, z});
  }

  uint64_t MaxSampleColumns() const
  {
    const auto span = [](int64_t lo, int64_t hi) {
      return 1 + static_cast<uint64_t>((hi - lo + CHUNK_SIZE - 1) / CHUNK_SIZE);
    };
    return span(min_x, max_x) * span(min_z, max_z);
  }

  uint64_t AlignedChunkColumns() const
  {
    // The source samples occupy offsets [0, CHUNK_SIZE - 1].
    const auto span = [](int64_t lo, int64_t hi) {
      return 1 + static_cast<uint64_t>((hi + CHUNK_SIZE - 1) / CHUNK_SIZE) +
             static_cast<uint64_t>((-lo + CHUNK_SIZE - 1) / CHUNK_SIZE);
    };
    return span(min_x, max_x) * span(min_z, max_z);
  }
};

struct WorldGenWriteExtents
{
  WorldGenWriteExtent synchronous_sample;
  WorldGenWriteExtent worker_sample;
};

} // namespace cutum
