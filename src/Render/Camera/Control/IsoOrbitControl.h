#ifndef ISOORBITCONTROL_H
#define ISOORBITCONTROL_H

#include "Render/Camera/Control/IUCameraOrientationControl.h"
#include <glm/glm.hpp>

namespace cutum
{

class UCamera;

class UIsoOrbitControl : public IUCameraOrientationControl
{
public:
  void ApplyMouseDelta(UCamera &camera, float x_offset, float y_offset,
                       bool constrain_pitch) override;
  void ApplyScroll(UCamera &camera, float y_offset) override;
  void SyncOrientation(UCamera &camera) override;
  void RotateYawIndex(UCamera &camera, int delta_steps);
};

/// Horizontal move intent in screen space projected onto XZ for isometric.
glm::vec3 IsoScreenMoveIntent(const UCamera &camera, float forward,
                              float rightward);

} // namespace cutum

#endif
