#include "Render/Camera/Control/IUGameplayViewController.h"

#include "Render/Camera/Control/FpsGameplayViewController.h"
#include "Render/Camera/Control/IsoGameplayViewController.h"

namespace cutum
{

const IUGameplayViewController &
GameplayViewControllerFor(bool isometric_projection)
{
  static const UFpsGameplayViewController kFps;
  static const UIsoGameplayViewController kIso;
  if (isometric_projection)
  {
    return kIso;
  }
  return kFps;
}

} // namespace cutum
