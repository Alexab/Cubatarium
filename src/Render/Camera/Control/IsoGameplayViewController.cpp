#include "Render/Camera/Control/IsoGameplayViewController.h"

#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraBasisLogic.h"
#include "Render/Camera/IsoViewPreset.h"
#include <cmath>

namespace cutum
{

namespace
{

glm::vec3 IsoLookDirection(const UCamera &camera)
{
  const float yaw =
      -90.0f + 45.0f + static_cast<float>(camera.GetIsoYawIndex()) * 90.0f;
  const float pitch = -camera.GetIsoPitchDeg();
  glm::vec3 front;
  glm::vec3 right;
  glm::vec3 up;
  const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
  ComputeFpsCameraBasis(yaw, pitch, front, right, up, world_up);
  return front;
}

glm::vec3 IsoScreenMoveIntent(const UCamera &camera, float forward,
                              float rightward)
{
  glm::vec3 front = IsoLookDirection(camera);
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

  glm::vec3 right =
      glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
  glm::vec3 intent = front * forward + right * rightward;
  const float intent_len = glm::length(intent);
  if (intent_len > 1.0e-6f)
  {
    intent /= intent_len;
  }
  return intent;
}

} // namespace

void UIsoGameplayViewController::ApplyLookDelta(UCamera &camera, float x_offset,
                                                float y_offset) const
{
  (void)y_offset;
  camera.AddAimYawDeg(x_offset * camera.GetMouseSensitivity());
}

void UIsoGameplayViewController::ApplyScroll(UCamera &camera,
                                             float y_offset) const
{
  camera.SetOrthoSize(camera.GetOrthoSize() - y_offset * 1.5f);
}

void UIsoGameplayViewController::CycleView(UCamera &camera) const
{
  camera.SetIsoViewPreset(CycleIsoViewPreset(camera.GetIsoViewPreset()));
}

void UIsoGameplayViewController::SnapCameraYaw(UCamera &camera,
                                               int delta_steps) const
{
  camera.RotateIsoYaw(delta_steps);
}

glm::vec3 UIsoGameplayViewController::ProjectMoveIntent(const UCamera &camera,
                                                        float forward,
                                                        float rightward) const
{
  return IsoScreenMoveIntent(camera, forward, rightward);
}

glm::vec3 UIsoGameplayViewController::ComputeCameraWorldPosition(
    const UCamera &camera) const
{
  const glm::vec3 focus = ComputeLookTarget(camera);
  const glm::vec3 dir = IsoLookDirection(camera);
  return focus - dir * camera.GetIsoBoomDistance();
}

glm::vec3
UIsoGameplayViewController::ComputeLookTarget(const UCamera &camera) const
{
  // Slightly below eye so the body sits in the lower half of the frame.
  return camera.GetPosition() - glm::vec3(0.0f, 0.6f, 0.0f);
}

CreatureViewOrientation UIsoGameplayViewController::ResolveCreatureOrientation(
    const UCamera &camera, const glm::vec3 &move_intent) const
{
  (void)move_intent;
  CreatureViewOrientation orient;
  orient.YawDeg = ModelYawFromCameraYaw(camera.GetAimYawDeg());
  orient.PitchDeg = 0.0f;
  return orient;
}

const char *
UIsoGameplayViewController::ViewLabel(const UCamera &camera) const
{
  return IsoViewPresetLabel(camera.GetIsoViewPreset());
}

} // namespace cutum
