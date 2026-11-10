#ifndef CAMERALENS_H
#define CAMERALENS_H

#include <glm/glm.hpp>

namespace cutum
{

namespace CameraLens
{

glm::mat4 BuildPerspective(float fov_deg, float aspect, float near_plane,
                           float far_plane);

/// Orthographic frustum centered on origin; @p ortho_size is half-height in
/// world units (zoom).
glm::mat4 BuildIsometricOrtho(float ortho_size, float aspect, float near_plane,
                              float far_plane);

} // namespace CameraLens

} // namespace cutum

#endif
