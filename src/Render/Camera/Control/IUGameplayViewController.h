#ifndef IUGAMEPLAYVIEWCONTROLLER_H
#define IUGAMEPLAYVIEWCONTROLLER_H

#include <glm/glm.hpp>

namespace cutum
{

class UCamera;

struct CreatureViewOrientation
{
  float YawDeg{0.0f};
  float PitchDeg{0.0f};
};

/// Projection-mode strategy for look, scroll, F5, move intent, and pose.
class IUGameplayViewController
{
public:
  virtual ~IUGameplayViewController() = default;

  virtual void ApplyLookDelta(UCamera &camera, float x_offset,
                              float y_offset) const = 0;
  virtual void ApplyScroll(UCamera &camera, float y_offset) const = 0;
  virtual void CycleView(UCamera &camera) const = 0;
  virtual void SnapCameraYaw(UCamera &camera, int delta_steps) const = 0;
  virtual glm::vec3 ProjectMoveIntent(const UCamera &camera, float forward,
                                      float rightward) const = 0;
  virtual glm::vec3 ComputeCameraWorldPosition(const UCamera &camera) const = 0;
  virtual glm::vec3 ComputeLookTarget(const UCamera &camera) const = 0;
  virtual CreatureViewOrientation
  ResolveCreatureOrientation(const UCamera &camera,
                             const glm::vec3 &move_intent) const = 0;
  virtual const char *ViewLabel(const UCamera &camera) const = 0;
};

const IUGameplayViewController &
GameplayViewControllerFor(bool isometric_projection);

} // namespace cutum

#endif
