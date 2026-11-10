#include "World/View/ViewRayMath.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{
namespace
{

bool IsFinite(const glm::vec3 &value)
{
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool IsFinite(const glm::mat4 &value)
{
  for (int column = 0; column < 4; ++column)
  {
    for (int row = 0; row < 4; ++row)
    {
      if (!std::isfinite(value[column][row]))
      {
        return false;
      }
    }
  }
  return true;
}

bool HomogeneousPoint(const glm::mat4 &transform, const glm::vec4 &point,
                      glm::vec3 &out_point)
{
  const glm::vec4 transformed = transform * point;
  if (!std::isfinite(transformed.w) || std::abs(transformed.w) < 1.0e-8f)
  {
    return false;
  }
  out_point = glm::vec3(transformed) / transformed.w;
  return IsFinite(out_point);
}

} // namespace

bool BuildScreenRayTransform(const glm::mat4 &view, const glm::mat4 &proj,
                             ScreenRayTransform &out_transform)
{
  out_transform = ScreenRayTransform{};
  out_transform.inverse_view = glm::inverse(view);
  out_transform.inverse_projection = glm::inverse(proj);
  out_transform.perspective = std::abs(proj[3][3]) < 1.0e-6f;
  out_transform.valid = IsFinite(out_transform.inverse_view) &&
                        IsFinite(out_transform.inverse_projection);
  return out_transform.valid;
}

bool ScreenNdcToCameraSpaceRay(const ScreenRayTransform &transform, float ndc_x,
                               float ndc_y, CameraSpaceRay &out_ray)
{
  out_ray = CameraSpaceRay{};
  if (!transform.valid || !std::isfinite(ndc_x) || !std::isfinite(ndc_y))
  {
    return false;
  }

  glm::vec3 view_near{};
  glm::vec3 view_far{};
  if (!HomogeneousPoint(transform.inverse_projection,
                        glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f), view_near) ||
      !HomogeneousPoint(transform.inverse_projection,
                        glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f), view_far))
  {
    return false;
  }

  const glm::vec3 view_direction =
      transform.perspective ? view_near : view_far - view_near;
  const float view_direction_length = glm::length(view_direction);
  if (!std::isfinite(view_direction_length) || view_direction_length < 1.0e-8f)
  {
    return false;
  }

  const glm::vec3 normalized_view_direction =
      view_direction / view_direction_length;
  const glm::vec3 world_direction =
      glm::mat3(transform.inverse_view) * normalized_view_direction;
  const float world_direction_length = glm::length(world_direction);
  if (!std::isfinite(world_direction_length) ||
      world_direction_length < 1.0e-8f)
  {
    return false;
  }

  if (!HomogeneousPoint(transform.inverse_view,
                        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                        out_ray.camera_origin) ||
      !HomogeneousPoint(transform.inverse_view, glm::vec4(view_near, 1.0f),
                        out_ray.near_origin))
  {
    out_ray = CameraSpaceRay{};
    return false;
  }

  out_ray.view_near = view_near;
  out_ray.view_far = view_far;
  out_ray.view_direction = normalized_view_direction;
  out_ray.direction = world_direction / world_direction_length;
  out_ray.near_distance = transform.perspective ? glm::length(view_near) : 0.0f;
  out_ray.segment_distance =
      glm::dot(view_far - view_near, normalized_view_direction);
  out_ray.perspective = transform.perspective;
  out_ray.origin =
      transform.perspective ? out_ray.camera_origin : out_ray.near_origin;

  if (!IsFinite(out_ray.direction) || !IsFinite(out_ray.origin) ||
      !std::isfinite(out_ray.near_distance) ||
      !std::isfinite(out_ray.segment_distance) ||
      out_ray.segment_distance <= 0.0f)
  {
    out_ray = CameraSpaceRay{};
    return false;
  }
  return true;
}

bool ReconstructSkyViewRay(const glm::mat4 &inverse_projection, float ndc_x,
                           float ndc_y, glm::vec3 &out_direction)
{
  out_direction = glm::vec3(0.0f, 0.0f, -1.0f);
  if (!IsFinite(inverse_projection) || !std::isfinite(ndc_x) ||
      !std::isfinite(ndc_y))
  {
    return false;
  }
  glm::vec3 near_point{};
  if (!HomogeneousPoint(inverse_projection,
                        glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f), near_point))
  {
    return false;
  }
  glm::vec3 direction = near_point;
  if (std::abs(inverse_projection[3][3]) > 1.0e-6f)
  {
    glm::vec3 far_point{};
    if (!HomogeneousPoint(inverse_projection,
                          glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f), far_point))
    {
      return false;
    }
    direction = far_point - near_point;
  }
  const float length = glm::length(direction);
  if (!std::isfinite(length) || length < 1.0e-8f)
  {
    return false;
  }
  out_direction = direction / length;
  return IsFinite(out_direction);
}

bool ScreenNdcDepthToCameraSpacePoint(const ScreenRayTransform &transform,
                                      float ndc_x, float ndc_y, float depth01,
                                      glm::vec3 &out_view_point,
                                      glm::vec3 &out_world_point)
{
  if (!transform.valid || !std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
      !std::isfinite(depth01) || depth01 < 0.0f || depth01 > 1.0f)
  {
    return false;
  }

  return HomogeneousPoint(transform.inverse_projection,
                          glm::vec4(ndc_x, ndc_y, depth01 * 2.0f - 1.0f, 1.0f),
                          out_view_point) &&
         HomogeneousPoint(transform.inverse_view,
                          glm::vec4(out_view_point, 1.0f), out_world_point);
}

bool ScreenPointToCameraSpaceRay(const ScreenRayTransform &transform,
                                 const glm::ivec4 &viewport, float screen_x,
                                 float screen_y, CameraSpaceRay &out_ray)
{
  if (!transform.valid || viewport.z <= 0 || viewport.w <= 0 ||
      !std::isfinite(screen_x) || !std::isfinite(screen_y))
  {
    out_ray = CameraSpaceRay{};
    return false;
  }

  const float ndc_x = ((screen_x - static_cast<float>(viewport.x)) /
                       static_cast<float>(viewport.z)) *
                          2.0f -
                      1.0f;
  const float ndc_y = ((screen_y - static_cast<float>(viewport.y)) /
                       static_cast<float>(viewport.w)) *
                          2.0f -
                      1.0f;
  return ScreenNdcToCameraSpaceRay(transform, ndc_x, ndc_y, out_ray);
}

bool ScreenPointToWorldRay(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::ivec4 &viewport, float mouse_x,
                           float mouse_y, glm::vec3 &out_origin,
                           glm::vec3 &out_dir)
{
  ScreenRayTransform transform{};
  CameraSpaceRay ray{};
  if (!BuildScreenRayTransform(view, proj, transform) ||
      !ScreenPointToCameraSpaceRay(transform, viewport, mouse_x, mouse_y, ray))
  {
    return false;
  }

  // Preserve the existing public contract: the returned origin lies on the
  // near plane for both perspective and orthographic projections.
  out_origin = ray.near_origin;
  out_dir = ray.direction;
  return true;
}

} // namespace cutum
