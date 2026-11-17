#pragma once

#include <algorithm>
#include <cstdlib>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{
inline std::vector<glm::ivec3> RequiredDataColumns(glm::ivec3 center,
                                                   int radius)
{
  std::vector<glm::ivec3> result;
  for (int ring = 0; ring <= radius; ++ring)
    for (int dz = -ring; dz <= ring; ++dz)
      for (int dx = -ring; dx <= ring; ++dx)
        if (std::max(std::abs(dx), std::abs(dz)) == ring)
          result.emplace_back(center.x + dx, 0, center.z + dz);
  return result;
}

/// Consumer-required data is independent of light and mesh readiness. Pending
/// owners do not consume admission slots, and admission never means completion.
template <typename Complete, typename Pending, typename Request>
int AdmitRequiredDataColumns(const std::vector<glm::ivec3> &columns, int budget,
                             Complete complete, Pending pending,
                             Request request)
{
  int admitted = 0;
  for (const auto &column : columns)
  {
    if (admitted >= budget)
      break;
    if (column.y != 0 || complete(column) || pending(column))
      continue;
    request(column);
    ++admitted;
  }
  return admitted;
}
} // namespace cutum
