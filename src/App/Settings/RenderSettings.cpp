#include "App/Settings/RenderSettings.h"
#include "App/Settings/GraphicsQualityProfile.h"

namespace cutum
{

RenderSettings RenderSettings::FromPreset(PerformancePreset preset)
{
  return GraphicsQualityProfile::FromPreset(preset).MakeRenderSettings();
}

} // namespace cutum
