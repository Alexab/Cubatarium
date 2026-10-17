#ifndef VIEWRAYMATH_H
#define VIEWRAYMATH_H

#include <glm/glm.hpp>

namespace cutum
{

/// Inverse matrices shared by the renderer's pixel witness and streaming's
/// screen-ray probe. Keeping view and projection inversions separate avoids
/// precision loss from a combined inverse view-projection at large coordinates.
struct ScreenRayTransform
{
  glm::mat4 inverse_view{1.0f};
  glm::mat4 inverse_projection{1.0f};
  bool perspective = true;
  bool valid = false;
};

/// Ray reconstructed in view space, then transformed into world space.
struct CameraSpaceRay
{
  glm::vec3 camera_origin{0.0f};
  glm::vec3 near_origin{0.0f};
  glm::vec3 origin{0.0f};
  glm::vec3 direction{0.0f, 0.0f, -1.0f};
  glm::vec3 view_near{0.0f};
  glm::vec3 view_far{0.0f};
  glm::vec3 view_direction{0.0f, 0.0f, -1.0f};
  float near_distance = 0.0f;
  float segment_distance = 0.0f;
  bool perspective = true;
};

bool BuildScreenRayTransform(const glm::mat4 &view, const glm::mat4 &proj,
                             ScreenRayTransform &out_transform);

/// Constructs a camera-space ray from normalized device coordinates.
bool ScreenNdcToCameraSpaceRay(const ScreenRayTransform &transform, float ndc_x,
                               float ndc_y, CameraSpaceRay &out_ray);

/// Reconstructs a GL depth-buffer sample without a combined inverse matrix.
bool ScreenNdcDepthToCameraSpacePoint(const ScreenRayTransform &transform,
                                     float ndc_x, float ndc_y,
                                     float depth01,
                                     glm::vec3 &out_view_point,
                                     glm::vec3 &out_world_point);

/// Constructs a camera-space ray from OpenGL window coordinates.
bool ScreenPointToCameraSpaceRay(const ScreenRayTransform &transform,
                                 const glm::ivec4 &viewport, float screen_x,
                                 float screen_y, CameraSpaceRay &out_ray);

/// Window-space point → world ray via separate inverse projection and view.
/// The output origin remains on the near plane for API compatibility.
bool ScreenPointToWorldRay(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::ivec4 &viewport, float mouse_x,
                           float mouse_y, glm::vec3 &out_origin,
                           glm::vec3 &out_dir);

} // namespace cutum

#endif
