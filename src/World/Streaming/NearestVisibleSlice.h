#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <optional>

namespace cutum
{

/// Keep the first nearest repair target, independent of coordinate signs.
struct NearestVisibleSlice
{
  std::optional<glm::ivec3> Coord;
  int64_t Distance{std::numeric_limits<int64_t>::max()};

  bool Consider(glm::ivec3 candidate, int64_t distance)
  {
    if (Coord && distance >= Distance)
      return false;
    Coord = candidate;
    Distance = distance;
    return true;
  }
};

} // namespace cutum
