#include "App/Settings/GraphicsQualityProfile.h"

namespace cutum
{

GraphicsQualityProfile
GraphicsQualityProfile::FromPreset(PerformancePreset preset)
{
  GraphicsQualityProfile profile;
  profile.Preset = preset;
  return profile;
}

PerformancePreset
GraphicsQualityProfile::ParseConfigString(const std::string &value)
{
  if (value == "performance")
  {
    return PerformancePreset::Performance;
  }
  if (value == "fast")
  {
    return PerformancePreset::Fast;
  }
  if (value == "quality")
  {
    return PerformancePreset::Quality;
  }
  return PerformancePreset::Balanced;
}

const char *GraphicsQualityProfile::ToConfigString(PerformancePreset preset)
{
  switch (preset)
  {
  case PerformancePreset::Performance:
    return "performance";
  case PerformancePreset::Fast:
    return "fast";
  case PerformancePreset::Quality:
    return "quality";
  case PerformancePreset::Balanced:
  default:
    return "balanced";
  }
}

const char *GraphicsQualityProfile::DisplayName(PerformancePreset preset)
{
  switch (preset)
  {
  case PerformancePreset::Performance:
    return "Performance";
  case PerformancePreset::Fast:
    return "Fast";
  case PerformancePreset::Quality:
    return "Quality";
  case PerformancePreset::Balanced:
  default:
    return "Balanced";
  }
}

PerformancePreset GraphicsQualityProfile::NextPreset(PerformancePreset preset)
{
  switch (preset)
  {
  case PerformancePreset::Performance:
    return PerformancePreset::Fast;
  case PerformancePreset::Fast:
    return PerformancePreset::Balanced;
  case PerformancePreset::Balanced:
    return PerformancePreset::Quality;
  case PerformancePreset::Quality:
  default:
    return PerformancePreset::Performance;
  }
}

LightingMode GraphicsQualityProfile::GetLightingMode() const
{
  return Preset == PerformancePreset::Performance ? LightingMode::Flat
                                                  : LightingMode::Full;
}

RenderSettings GraphicsQualityProfile::MakeRenderSettings() const
{
  RenderSettings s;
  s.Preset = Preset;
  switch (Preset)
  {
  case PerformancePreset::Performance:
    s.AsyncMeshing = false;
    s.FrustumCulling = true;
    s.DistanceFog = false;
    s.GradientSky = false;
    s.BatchCache = true;
    s.FogPullInEnabled = false;
    s.FogWaterUnfinishedBoost = false;
    break;
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

void GraphicsQualityProfile::ApplyTo(RenderSettings &settings) const
{
  settings = MakeRenderSettings();
}

} // namespace cutum
