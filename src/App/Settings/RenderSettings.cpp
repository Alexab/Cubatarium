#include "App/Settings/RenderSettings.h"

namespace cutum
{

RenderSettings RenderSettings::FromPreset(PerformancePreset preset)
{
  RenderSettings s;
  s.Preset = preset;
  switch (preset)
  {
  case PerformancePreset::Fast:
    s.AsyncMeshing = false;
    s.FrustumCulling = true;
    s.DistanceFog = false;
    s.GradientSky = false;
    s.BatchCache = true;
    break;
  case PerformancePreset::Quality:
    s.AsyncMeshing = true;
    s.FrustumCulling = true;
    s.DistanceFog = true;
    s.GradientSky = true;
    s.BatchCache = true;
    break;
  case PerformancePreset::Balanced:
  default:
    s = RenderSettings{};
    s.Preset = PerformancePreset::Balanced;
    break;
  }
  return s;
}

} // namespace cutum
