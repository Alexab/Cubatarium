#include "Render/Camera/CameraLens.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{
namespace CameraLens
{

glm::mat4 BuildPerspective(float fov_deg, float aspect, float near_plane,
                           float far_plane)
{
  const float safe_aspect = std::max(aspect, 1.0e-3f);
  return glm::perspective(glm::radians(fov_deg), safe_aspect, near_plane,
                          far_plane);
}

glm::mat4 BuildIsometricOrtho(float ortho_size, float aspect, float near_plane,
                              float far_plane)
{
  const float half_h = std::max(ortho_size, 1.0e-3f);
  const float safe_aspect = std::max(aspect, 1.0e-3f);
  const float half_w = half_h * safe_aspect;
  return glm::ortho(-half_w, half_w, -half_h, half_h, near_plane, far_plane);
}

} // namespace CameraLens
} // namespace cutum
