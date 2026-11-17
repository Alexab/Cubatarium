#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <optional>

namespace cutum
{
struct DrawWorldBounds
{
  glm::vec3 low, high;
};

/// Unknown/non-affine envelopes stay explicit; never infer a unit box.
inline std::optional<DrawWorldBounds>
TransformDrawWorldBounds(glm::vec3 local_low, glm::vec3 local_high,
                         const glm::mat4 &model)
{
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(local_low[axis]) || !std::isfinite(local_high[axis]) ||
        local_low[axis] > local_high[axis])
      return std::nullopt;
  glm::dvec3 low(std::numeric_limits<double>::infinity());
  glm::dvec3 high(-std::numeric_limits<double>::infinity());
  for (int x : {0, 1})
    for (int y : {0, 1})
      for (int z : {0, 1})
      {
        const auto point =
            glm::dmat4(model) * glm::dvec4(x ? local_high.x : local_low.x,
                                           y ? local_high.y : local_low.y,
                                           z ? local_high.z : local_low.z, 1);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z) || point.w != 1)
          return std::nullopt;
        low = glm::min(low, glm::dvec3(point));
        high = glm::max(high, glm::dvec3(point));
      }
  DrawWorldBounds result;
  for (int axis = 0; axis < 3; ++axis)
  {
    // Round outward so world-coordinate precision cannot shrink the envelope.
    result.low[axis] = std::nextafter(static_cast<float>(low[axis]),
                                      -std::numeric_limits<float>::infinity());
    result.high[axis] = std::nextafter(static_cast<float>(high[axis]),
                                       std::numeric_limits<float>::infinity());
    if (!std::isfinite(result.low[axis]) || !std::isfinite(result.high[axis]))
      return std::nullopt;
  }
  return result;
}

inline bool BoundsIntersectDrawDistance(const DrawWorldBounds &bounds,
                                        glm::vec3 camera, float distance)
{
  if (!std::isfinite(distance) || distance <= 0)
    return true;
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(camera[axis]) || !std::isfinite(bounds.low[axis]) ||
        !std::isfinite(bounds.high[axis]) ||
        bounds.low[axis] > bounds.high[axis])
      return true;
  const auto eye = glm::dvec3(camera);
  const auto offset =
      glm::clamp(eye, glm::dvec3(bounds.low), glm::dvec3(bounds.high)) - eye;
  const double radius = distance + 1e-5 * std::max(1.f, distance);
  return glm::dot(offset, offset) <= radius * radius;
}
} // namespace cutum
