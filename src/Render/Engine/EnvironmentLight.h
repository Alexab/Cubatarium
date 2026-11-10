#ifndef ENVIRONMENTLIGHT_H
#define ENVIRONMENTLIGHT_H

#include <algorithm>

namespace cutum
{

/// Sky light attenuation shared by the block and creature shaders.
inline float SkyLightScaleFromWeather(float weather_sky_attenuation,
                                      float precipitation_intensity)
{
  float scale = weather_sky_attenuation;
  if (precipitation_intensity > 0.05f)
  {
    scale *= 1.0f - std::clamp(precipitation_intensity, 0.0f, 1.0f) * 0.12f;
  }
  return std::clamp(scale, 0.82f, 1.0f);
}

} // namespace cutum

#endif
