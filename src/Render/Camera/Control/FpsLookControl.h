#ifndef FPSLOOKCONTROL_H
#define FPSLOOKCONTROL_H

#include "Render/Camera/Control/IUCameraOrientationControl.h"

namespace cutum
{

class UFpsLookControl : public IUCameraOrientationControl
{
public:
  void ApplyMouseDelta(UCamera &camera, float x_offset, float y_offset,
                       bool constrain_pitch) override;
  void ApplyScroll(UCamera &camera, float y_offset) override;
  void SyncOrientation(UCamera &camera) override;
};

} // namespace cutum

#endif
