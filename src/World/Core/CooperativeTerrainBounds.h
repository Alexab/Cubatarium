#pragma once

#include "WorldGen/Core/WorldGenWriteExtent.h"
#include <cstddef>
#include <optional>

namespace cutum
{

struct TerrainColumnRectangle
{
  int64_t min_x{0}, max_x{-1}, min_z{0}, max_z{-1};

  static TerrainColumnRectangle FromBlocks(int64_t min_x, int64_t max_x,
                                           int64_t min_z, int64_t max_z)
  {
    const auto chunk = [](int64_t p) {
      return p / CHUNK_SIZE - (p % CHUNK_SIZE < 0 ? 1 : 0);
    };
    return {chunk(min_x), chunk(max_x), chunk(min_z), chunk(max_z)};
  }

  bool Contains(int64_t x, int64_t z) const
  {
    return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
  }

  uint64_t Count() const
  {
    if (min_x > max_x || min_z > max_z)
      return 0;
    return static_cast<uint64_t>(max_x - min_x + 1) *
           static_cast<uint64_t>(max_z - min_z + 1);
  }

  TerrainColumnRectangle Intersect(const TerrainColumnRectangle &other) const
  {
    return {std::max(min_x, other.min_x), std::min(max_x, other.max_x),
            std::max(min_z, other.min_z), std::min(max_z, other.max_z)};
  }
};

struct CooperativeTerrainBounds
{
  // The legacy scan has no spatial restriction. Each enumerated candidate
  // can supply at most one XZ, even when several files use the same XZ.
  size_t legacy_file_candidates{0};
  TerrainColumnRectangle spatial_disk;
  std::optional<TerrainColumnRectangle> synchronous;
  std::optional<TerrainColumnRectangle> worker;

  std::optional<uint64_t> CombinedUpper(bool parallel_generation) const
  {
    const auto &generation = parallel_generation ? worker : synchronous;
    if (!generation)
      return std::nullopt;
    return legacy_file_candidates + spatial_disk.Count() +
           (generation->Count() - generation->Intersect(spatial_disk).Count());
  }
};

} // namespace cutum
