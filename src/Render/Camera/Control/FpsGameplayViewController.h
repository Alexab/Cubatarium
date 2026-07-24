#ifndef FPSGAMEPLAYVIEWCONTROLLER_H
#define FPSGAMEPLAYVIEWCONTROLLER_H

#include "Render/Camera/Control/IUGameplayViewController.h"

namespace cutum
{

class UFpsGameplayViewController : public IUGameplayViewController
{
public:
  void ApplyLookDelta(UCamera &camera, float x_offset,
                      float y_offset) const override;
  void ApplyScroll(UCamera &camera, float y_offset) const override;
  void CycleView(UCamera &camera) const override;
  void SnapCameraYaw(UCamera &camera, int delta_steps) const override;
  glm::vec3 ProjectMoveIntent(const UCamera &camera, float forward,
                              float rightward) const override;
  glm::vec3 ComputeCameraWorldPosition(const UCamera &camera) const override;
  glm::vec3 ComputeLookTarget(const UCamera &camera) const override;
  CreatureViewOrientation
  ResolveCreatureOrientation(const UCamera &camera,
                             const glm::vec3 &move_intent) const override;
  const char *ViewLabel(const UCamera &camera) const override;
};

} // namespace cutum

#endif
