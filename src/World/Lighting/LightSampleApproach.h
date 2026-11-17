#pragma once

#include "World/Chunks/ChunkDimensions.h"
#include "World/Math/WorldVerticalProfile.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

namespace cutum
{
/// One second of measured motion, capped at two sections per axis. At most
/// 27 approach sections; static consumers request only their current section.
template <typename Visit>
int VisitLightSampleApproach(glm::ivec3 cell, glm::vec3 velocity,
                             const WorldVerticalProfile &profile, Visit visit)
{
  if (!profile.IsValid())
    return 0;
  glm::ivec3 begin, end;
  for (int axis = 0; axis < 3; ++axis)
  {
    begin[axis] = FloorDiv(cell[axis], CHUNK_SIZE);
    const double delta = std::isfinite(velocity[axis])
                             ? std::clamp(static_cast<double>(velocity[axis]),
                                          -2.0 * CHUNK_SIZE, 2.0 * CHUNK_SIZE)
                             : 0;
    // The input cell has lost the sub-voxel position. Round toward motion so
    // a slow consumer next to its cell boundary is still primed before entry.
    const auto offset =
        static_cast<int64_t>(delta > 0 ? std::ceil(delta) : std::floor(delta));
    const auto future = std::clamp<int64_t>(
        static_cast<int64_t>(cell[axis]) + offset,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    end[axis] = FloorDiv(static_cast<int>(future), CHUNK_SIZE);
  }
  int count = 0;
  const auto emit = [&](glm::ivec3 coord)
  {
    if (profile.SliceBounds(coord.y, CHUNK_SIZE))
    {
      visit(coord);
      ++count;
    }
  };
  emit(begin);
  for (int y = std::min(begin.y, end.y); y <= std::max(begin.y, end.y); ++y)
    for (int z = std::min(begin.z, end.z); z <= std::max(begin.z, end.z); ++z)
      for (int x = std::min(begin.x, end.x); x <= std::max(begin.x, end.x); ++x)
      {
        const glm::ivec3 coord(x, y, z);
        if (coord != begin)
          emit(coord);
      }
  return count;
}
} // namespace cutum
