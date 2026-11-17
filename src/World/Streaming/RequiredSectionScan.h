#pragma once

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

namespace cutum
{
// A bounded maintenance walk, never a readiness authority. Relative offsets
// keep advancing when focus moves, so movement cannot restart/starve the rim.
class RequiredSectionScan
{
public:
  void Reset()
  {
    Radius = -1;
    Cursor = 0;
  }

  template <typename Visit>
  int Drain(glm::ivec3 center, int radius, int min_cy, int max_cy, int budget,
            Visit visit)
  {
    if (radius < 0 || min_cy > max_cy || budget <= 0)
      return 0;
    const uint64_t side = uint64_t(radius) * 2 + 1;
    const uint64_t height = uint64_t(int64_t(max_cy) - min_cy) + 1;
    const auto limit = std::numeric_limits<uint64_t>::max();
    if (side > limit / side || side * side > limit / height)
      return 0;
    const uint64_t total = side * side * height;
    if (Radius != radius || MinCy != min_cy || MaxCy != max_cy)
    {
      Radius = radius;
      MinCy = min_cy;
      MaxCy = max_cy;
      Cursor = 0;
    }
    const int count = static_cast<int>(std::min(uint64_t(budget), total));
    for (int n = 0; n < count; ++n)
    {
      const uint64_t index = Cursor;
      Cursor = (Cursor + 1) % total;
      const uint64_t column = index / height;
      const int dx = static_cast<int>(int64_t(column % side) - radius);
      const int dz = static_cast<int>(int64_t(column / side) - radius);
      const int cy =
          static_cast<int>(int64_t(min_cy) + int64_t(index % height));
      visit(glm::ivec3(center.x + dx, cy, center.z + dz));
    }
    return count;
  }

private:
  int Radius{-1}, MinCy{0}, MaxCy{-1};
  uint64_t Cursor{0};
};
} // namespace cutum
