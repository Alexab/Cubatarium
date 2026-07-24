#ifndef ICAMERAORIENTATIONCONTROL_H
#define ICAMERAORIENTATIONCONTROL_H

namespace cutum
{

class UCamera;

/// Orientation / zoom input for a camera projection mode.
class IUCameraOrientationControl
{
public:
  virtual ~IUCameraOrientationControl() = default;
  virtual void ApplyMouseDelta(UCamera &camera, float x_offset, float y_offset,
                               bool constrain_pitch) = 0;
  virtual void ApplyScroll(UCamera &camera, float y_offset) = 0;
  virtual void SyncOrientation(UCamera &camera) = 0;
};

} // namespace cutum

#endif
