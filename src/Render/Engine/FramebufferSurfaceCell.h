#pragma once

#include "World/Math/GridMath.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum
{
// Framebuffer depth locates the visible face, whose voxel centers are integers.
// Bias along the viewing ray into the solid before choosing the owning cell;
// floor(point) selects the previous cell on tangential half-block intervals.
inline glm::ivec3 FramebufferSurfaceCell(glm::vec3 point, glm::vec3 unit_ray)
{
  const auto inside = point + unit_ray * 0.002f;
  return glm::ivec3(WorldCoordToBlockIndex(inside.x),
                    WorldCoordToBlockIndex(inside.y),
                    WorldCoordToBlockIndex(inside.z));
}

struct FramebufferSurfaceCandidates
{
  std::array<glm::ivec3, 8> cells{};
  size_t count{0};
};

// Depth reconstruction can straddle a half-integer plane, especially with a
// grazing ray: a ray-length bias gives almost no normal displacement. These
// are candidates ONLY, never evidence of ownership. The caller must match a
// triangle actually submitted for this frame. Keep the original cell first.
inline FramebufferSurfaceCandidates
FramebufferSurfaceCandidateCells(glm::vec3 point, glm::vec3 unit_ray)
{
  FramebufferSurfaceCandidates result;
  result.cells[result.count++] = FramebufferSurfaceCell(point, unit_ray);
  constexpr float uncertainty = 0.002f;
  glm::ivec3 low, high;
  for (int axis = 0; axis < 3; ++axis)
  {
    low[axis] = WorldCoordToBlockIndex(point[axis] - uncertainty);
    high[axis] = WorldCoordToBlockIndex(point[axis] + uncertainty);
  }
  for (int x = low.x; x <= high.x; ++x)
    for (int y = low.y; y <= high.y; ++y)
      for (int z = low.z; z <= high.z; ++z)
      {
        const glm::ivec3 cell(x, y, z);
        bool duplicate = false;
        for (size_t i = 0; i < result.count; ++i)
          duplicate |= result.cells[i] == cell;
        if (!duplicate && result.count < result.cells.size())
          result.cells[result.count++] = cell;
      }
  return result;
}

struct FramebufferSourceRecovery
{
  glm::ivec3 coord{0};
  size_t tried{1}, matches{0};
  bool Unique() const { return matches == 1; }
};

// The probe counts actual submitted draw owners, not known voxels. A CPU
// vertex owner and a packed owner at the same candidate are distinct matches.
// Deduplicate section coordinates before probing seam/corner candidates.
template <typename Probe>
FramebufferSourceRecovery
RecoverFramebufferSurfaceSource(const FramebufferSurfaceCandidates &candidates,
                                Probe probe)
{
  FramebufferSourceRecovery result;
  std::array<glm::ivec3, 8> tried{};
  const auto section = [](glm::ivec3 cell)
  {
    return glm::ivec3(FloorDiv(cell.x, 16), FloorDiv(cell.y, 16),
                      FloorDiv(cell.z, 16));
  };
  tried[0] = section(candidates.cells[0]);
  for (size_t i = 1; i < candidates.count; ++i)
  {
    const auto coord = section(candidates.cells[i]);
    bool duplicate = false;
    for (size_t j = 0; j < result.tried; ++j)
      duplicate |= tried[j] == coord;
    if (duplicate)
      continue;
    tried[result.tried++] = coord;
    const size_t matches = probe(coord);
    result.matches += matches;
    if (matches)
      result.coord = coord;
  }
  return result;
}
} // namespace cutum
