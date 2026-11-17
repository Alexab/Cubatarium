#pragma once

#include "World/Streaming/RequiredDataCoverage.h"
#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{
// The configured render ring has a one-column approach ring. Mesh consumers
// read neighboring light, whose calculations in turn read neighboring data.
struct RequiredWorldDomain
{
  int presentation{1};
  int approach{2};
  int light{3};
  int data{4};

  static RequiredWorldDomain ForRenderDistance(int radius, bool physical_light)
  {
    RequiredWorldDomain out;
    out.presentation = std::max(1, radius);
    out.approach = out.presentation + 1;
    out.light = out.approach + (physical_light ? 1 : 0);
    out.data = out.light + 1;
    return out;
  }
};

// A world-local, explicit consumer lease, not a historical residency flag.
// It moves with demand and protects both data residency and queued producers.
struct RequiredDataLease
{
  glm::ivec3 center{0};
  int radius{-1};

  bool Contains(glm::ivec3 column) const
  {
    return radius >= 0 && std::max(std::abs(column.x - center.x),
                                   std::abs(column.z - center.z)) <= radius;
  }
  int RetentionRadiusAround(glm::ivec3 origin, int ordinary_radius) const
  {
    if (radius < 0)
      return ordinary_radius;
    const int offset =
        std::max(std::abs(origin.x - center.x), std::abs(origin.z - center.z));
    return std::max(ordinary_radius, radius + offset);
  }
};

// Rotate relative offsets rather than restarting at the center on movement.
// Rapid focus changes cannot indefinitely starve the outer dependency halo.
class RequiredLightScan
{
public:
  template <typename Visit>
  int Drain(glm::ivec3 center, int radius, int budget, Visit visit)
  {
    if (radius != Radius)
    {
      Radius = radius;
      Offsets = RequiredDataColumns({0, 0, 0}, radius);
      Cursor = 0;
    }
    const int count =
        std::min(std::max(0, budget), static_cast<int>(Offsets.size()));
    for (int n = 0; n < count; ++n)
    {
      const auto offset = Offsets[Cursor];
      Cursor = (Cursor + 1) % Offsets.size();
      visit(glm::ivec3(center.x + offset.x, 0, center.z + offset.z));
    }
    return count;
  }

private:
  int Radius{-1};
  std::size_t Cursor{0};
  std::vector<glm::ivec3> Offsets;
};
} // namespace cutum
