#ifndef CREATURESEPARATIONMATH_H
#define CREATURESEPARATIONMATH_H

#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

/// Horizontal MTV push to separate two axis-aligned footprints on XZ.
inline std::optional<glm::vec2>
ComputeOverlapSeparationPushXZ(float selfCx, float selfCz, float selfHalfX,
                               float selfHalfZ, float otherCx, float otherCz,
                               float otherHalfX, float otherHalfZ,
                               float epsilon = 0.05f)
{
  const float penX =
      (selfHalfX + otherHalfX) - std::abs(selfCx - otherCx);
  const float penZ =
      (selfHalfZ + otherHalfZ) - std::abs(selfCz - otherCz);
  if (penX <= 0.0f || penZ <= 0.0f)
  {
    return std::nullopt;
  }
  glm::vec2 push(0.0f);
  if (penX <= penZ)
  {
    const float sign = selfCx >= otherCx ? 1.0f : -1.0f;
    push.x = sign * (penX + epsilon);
  }
  else
  {
    const float sign = selfCz >= otherCz ? 1.0f : -1.0f;
    push.y = sign * (penZ + epsilon);
  }
  return push;
}

} // namespace cutum

#endif
