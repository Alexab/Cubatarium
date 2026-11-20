#include "Render/Camera/Camera.h"
#include <stdexcept>

// These tests exercise mesh mutation delivery without constructing a camera.
// Fail on accidental rendering use rather than inventing camera evidence.
namespace cutum
{
glm::mat4 UCamera::GetProjection() const
{
  throw std::logic_error("camera projection is outside the mesh-service contract fixture");
}
glm::mat4 UCamera::GetViewMatrix() const
{
  throw std::logic_error("camera view is outside the mesh-service contract fixture");
}
} // namespace cutum
