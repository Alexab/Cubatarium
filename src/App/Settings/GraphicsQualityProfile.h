#ifndef GRAPHICSQUALITYPROFILE_H
#define GRAPHICSQUALITYPROFILE_H

#include "App/Settings/RenderSettings.h"
#include <string>

namespace cutum
{

enum class LightingMode
{
  Full,
  Flat
};

/// Single source of truth for graphics quality presets: render seeds + lighting
/// backend selection. Config key remains `render.performance_preset`.
struct GraphicsQualityProfile
{
  PerformancePreset Preset{PerformancePreset::Balanced};

  static GraphicsQualityProfile FromPreset(PerformancePreset preset);
  static PerformancePreset ParseConfigString(const std::string &value);
  static const char *ToConfigString(PerformancePreset preset);
  static const char *DisplayName(PerformancePreset preset);
  /// Cycle Performance → Fast → Balanced → Quality → Performance.
  static PerformancePreset NextPreset(PerformancePreset preset);

  LightingMode GetLightingMode() const;
  RenderSettings MakeRenderSettings() const;
  void ApplyTo(RenderSettings &settings) const;
};

} // namespace cutum

#endif
