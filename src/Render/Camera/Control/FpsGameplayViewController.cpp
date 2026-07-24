#include "Render/Camera/Control/FpsGameplayViewController.h"

#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraPerspective.h"
#include <cmath>

namespace cutum
{

namespace
{

float Radians(float degrees)
{
  return degrees * 0.01745329251994329576923690768489f;
}

} // namespace

void UFpsGameplayViewController::ApplyLookDelta(UCamera &camera, float x_offset,
                                                float y_offset) const
{
  camera.ApplyFpsLookDelta(x_offset, y_offset, true);
}

void UFpsGameplayViewController::ApplyScroll(UCamera &camera,
                                             float y_offset) const
{
  camera.ApplyFpsZoomScroll(y_offset);
}

void UFpsGameplayViewController::CycleView(UCamera &camera) const
{
  camera.CycleFpsPerspective();
}

void UFpsGameplayViewController::SnapCameraYaw(UCamera &camera,
                                               int delta_steps) const
{
  (void)camera;
  (void)delta_steps;
}

glm::vec3 UFpsGameplayViewController::ProjectMoveIntent(const UCamera &camera,
                                                        float forward,
                                                        float rightward) const
{
  glm::vec3 shift(0.0f);
  if (forward != 0.0f)
  {
    shift += glm::vec3(std::cos(Radians(camera.GetYaw())), 0.0f,
                       std::sin(Radians(camera.GetYaw()))) *
             forward;
  }
  if (rightward != 0.0f)
  {
    shift += camera.GetRight() * rightward;
  }
  shift.y = 0.0f;
  const float len = glm::length(shift);
  if (len > 1.0e-6f)
  {
    shift /= len;
  }
  return shift;
}

glm::vec3 UFpsGameplayViewController::ComputeCameraWorldPosition(
    const UCamera &camera) const
{
  const glm::vec3 eye = camera.GetPosition();
  if (camera.GetPerspective() == CameraPerspective::FirstPerson)
  {
    return eye;
  }
  if (camera.GetPerspective() == CameraPerspective::ThirdPersonBack)
  {
    return eye - camera.GetFront() * camera.GetThirdPersonDistance() +
           camera.GetUp() * camera.GetThirdPersonHeight();
  }
  return eye + camera.GetFront() * camera.GetThirdPersonDistance();
}

glm::vec3
UFpsGameplayViewController::ComputeLookTarget(const UCamera &camera) const
{
  return camera.GetPosition() + camera.GetFront();
}

CreatureViewOrientation UFpsGameplayViewController::ResolveCreatureOrientation(
    const UCamera &camera, const glm::vec3 &move_intent) const
{
  (void)move_intent;
  CreatureViewOrientation orient;
  orient.YawDeg = ModelYawFromCameraYaw(camera.GetYaw());
  orient.PitchDeg = camera.GetPitch();
  return orient;
}

const char *
UFpsGameplayViewController::ViewLabel(const UCamera &camera) const
{
  return CameraPerspectiveLabel(camera.GetPerspective());
}

} // namespace cutum
