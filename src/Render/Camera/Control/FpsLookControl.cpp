#include "Render/Camera/Control/FpsLookControl.h"

#include "Render/Camera/Camera.h"

namespace cutum
{

void UFpsLookControl::ApplyMouseDelta(UCamera &camera, float x_offset,
                                      float y_offset, bool constrain_pitch)
{
  (void)constrain_pitch;
  if (camera.IsIsometricProjection())
  {
    return;
  }
  camera.ApplyRelativeMouseMove(x_offset, y_offset);
}

void UFpsLookControl::ApplyScroll(UCamera &camera, float y_offset)
{
  if (camera.IsIsometricProjection())
  {
    return;
  }
  camera.UpdateMouseScroll(0.0, static_cast<double>(y_offset));
}

void UFpsLookControl::SyncOrientation(UCamera &camera)
{
  (void)camera;
}

} // namespace cutum
