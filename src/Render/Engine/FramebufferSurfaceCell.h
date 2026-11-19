#pragma once

#include "World/Math/GridMath.h"
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
} // namespace cutum
