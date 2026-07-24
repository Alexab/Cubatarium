#include "Render/Camera/Control/IsoOrbitControl.h"

#include "Render/Camera/Camera.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

void UIsoOrbitControl::ApplyMouseDelta(UCamera &camera, float x_offset,
                                       float y_offset, bool constrain_pitch)
{
  if (!camera.IsIsometricProjection())
  {
    return;
  }
  camera.ApplyRelativeMouseMove(x_offset, y_offset);
  (void)constrain_pitch;
}

void UIsoOrbitControl::ApplyScroll(UCamera &camera, float y_offset)
{
  const float next = camera.GetOrthoSize() - y_offset * 1.5f;
  camera.SetOrthoSize(next);
}

void UIsoOrbitControl::SyncOrientation(UCamera &camera)
{
  camera.SetIsoYawIndex(camera.GetIsoYawIndex());
}

void UIsoOrbitControl::RotateYawIndex(UCamera &camera, int delta_steps)
{
  camera.SetIsoYawIndex(camera.GetIsoYawIndex() + delta_steps);
}

glm::vec3 IsoScreenMoveIntent(const UCamera &camera, float forward,
                              float rightward)
{
  glm::vec3 front = camera.GetFront();
  front.y = 0.0f;
  const float front_len = glm::length(front);
  if (front_len > 1.0e-6f)
  {
    front /= front_len;
  }
  else
  {
    front = glm::vec3(0.0f, 0.0f, -1.0f);
  }

  glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
  glm::vec3 intent = front * forward + right * rightward;
  const float intent_len = glm::length(intent);
  if (intent_len > 1.0e-6f)
  {
    intent /= intent_len;
  }
  return intent;
}

} // namespace cutum
