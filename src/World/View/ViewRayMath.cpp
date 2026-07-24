#include "World/View/ViewRayMath.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

bool ScreenPointToWorldRay(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::ivec4 &viewport, float mouse_x,
                           float mouse_y, glm::vec3 &out_origin,
                           glm::vec3 &out_dir)
{
  if (viewport.z <= 0 || viewport.w <= 0)
  {
    return false;
  }

  const glm::vec3 near_point = glm::unProject(
      glm::vec3(mouse_x, mouse_y, 0.0f), view, proj, viewport);
  const glm::vec3 far_point = glm::unProject(
      glm::vec3(mouse_x, mouse_y, 1.0f), view, proj, viewport);
  out_origin = near_point;
  out_dir = far_point - near_point;
  const float len = glm::length(out_dir);
  if (!std::isfinite(len) || len < 1.0e-8f)
  {
    return false;
  }
  out_dir /= len;
  return std::isfinite(out_origin.x) && std::isfinite(out_origin.y) &&
         std::isfinite(out_origin.z);
}

} // namespace cutum
