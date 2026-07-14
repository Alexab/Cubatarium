#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

inline float CameraDegreesToRadians(float degrees)
{
  return degrees * 0.017453292519943295f;
}

/// FPS camera basis: horizontal right from yaw avoids gimbal snap near ±90°
/// pitch.
inline void
ComputeFpsCameraBasis(float yaw_deg, float pitch_deg, glm::vec3 &front,
                      glm::vec3 &right, glm::vec3 &up,
                      const glm::vec3 &world_up = glm::vec3(0.0f, 1.0f, 0.0f))
{
  (void)world_up;
  front.x = std::cos(CameraDegreesToRadians(yaw_deg)) *
            std::cos(CameraDegreesToRadians(pitch_deg));
  front.y = std::sin(CameraDegreesToRadians(pitch_deg));
  front.z = std::sin(CameraDegreesToRadians(yaw_deg)) *
            std::cos(CameraDegreesToRadians(pitch_deg));
  const float frontLen = glm::length(front);
  if (frontLen > 1.0e-6f)
  {
    front /= frontLen;
  }
  else
  {
    front = glm::vec3(0.0f, 0.0f, -1.0f);
  }

  right = glm::vec3(-std::sin(CameraDegreesToRadians(yaw_deg)), 0.0f,
                    std::cos(CameraDegreesToRadians(yaw_deg)));
  const float rightLen = glm::length(right);
  if (rightLen > 1e-6f)
  {
    right /= rightLen;
  }
  else
  {
    right = glm::vec3(1.0f, 0.0f, 0.0f);
  }

  up = glm::cross(right, front);
  const float upLen = glm::length(up);
  if (upLen > 1.0e-6f)
  {
    up /= upLen;
  }
  else
  {
    up = world_up;
  }
}

} // namespace cutum
